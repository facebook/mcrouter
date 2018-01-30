/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/IOBufQueue.h>

#include "mcrouter/lib/debug/ConnectionFifo.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

/*
 * Determine the protocol by looking at the first byte
 */
inline mc_protocol_t determineProtocol(uint8_t firstByte) {
  switch (firstByte) {
    case kCaretMagicByte:
      return mc_caret_protocol;
    case ENTRY_LIST_MAGIC_BYTE:
      return mc_umbrella_protocol_DONOTUSE;
    default:
      return mc_ascii_protocol;
  }
}

class McParser {
 public:
  class ParserCallback {
   public:
    virtual ~ParserCallback() = 0;

    /**
     * We fully parsed an umbrella message and want to call RequestReady or
     * ReplyReady callback.
     *
     * @param info        Message information
     * @param buffer      Coalesced IOBuf that holds the entire message
     *                    (header and body)
     * @return            False on any parse errors.
     */
    virtual bool umMessageReady(
        const UmbrellaMessageInfo& info,
        const folly::IOBuf& buffer) = 0;

    /**
     * caretMessageReady should be called after we have successfully parsed the
     * Caret header and after the full Caret message body is in the read buffer.
     *
     * @param headerInfo  Parsed header data (header size, body size, etc.)
     * @param buffer      Coalesced IOBuf that holds the entire message
     *                    (header and body)
     * @return            False on any parse errors.
     */
    virtual bool caretMessageReady(
        const UmbrellaMessageInfo& headerInfo,
        const folly::IOBuf& buffer) = 0;

    /**
     * Handle ascii data read.
     * The user is responsible for clearing or advancing the readBuffer.
     *
     * @param readBuffer  buffer with newly read data that needs to be parsed.
     */
    virtual void handleAscii(folly::IOBuf& readBuffer) = 0;

    /**
     * Called on fatal parse error (the stream should normally be closed)
     */
    virtual void parseError(mc_res_t result, folly::StringPiece reason) = 0;
  };

  McParser(
      ParserCallback& cb,
      size_t minBufferSize,
      size_t maxBufferSize,
      const bool useJemallocNodumpAllocator = false,
      ConnectionFifo* debugFifo = nullptr);

  ~McParser() = default;

  mc_protocol_t protocol() const {
    return protocol_;
  }

  void setProtocol(mc_protocol_t protocol__) {
    protocol_ = protocol__;
  }

  bool outOfOrder() const {
    return outOfOrder_;
  }

  /**
   * TAsyncTransport-style getReadBuffer().
   *
   * Returns a buffer pointer and its size that should be safe
   * to read into.
   * The caller might use less than the whole buffer, and will call
   * readDataAvailable(n) giving the actual number of bytes used from
   * the beginning of this buffer.
   */
  std::pair<void*, size_t> getReadBuffer();

  /**
   * Feeds the new data into the parser.
   * @return false  On any parse error.
   */
  bool readDataAvailable(size_t len);

  double getDropProbability() const;

  void reset();

 private:
  bool seenFirstByte_{false};
  bool outOfOrder_{false};

  mc_protocol_t protocol_{mc_unknown_protocol};

  ParserCallback& callback_;
  size_t bufferSize_{256};
  size_t maxBufferSize_{4096};

  ConnectionFifo* debugFifo_{nullptr};

  uint64_t lastShrinkCycles_{0};

  folly::IOBuf readBuffer_;

  /**
   * If we've read an umbrella header, this will contain header/body sizes.
   */
  UmbrellaMessageInfo umMsgInfo_;

  /**
   * Custom allocator states and method
   */
  bool useJemallocNodumpAllocator_{false};

  bool readUmbrellaOrCaretData();
};

inline McParser::ParserCallback::~ParserCallback() {}
}
} // facebook::memcache
