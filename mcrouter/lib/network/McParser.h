/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/IOBufQueue.h>

#include "mcrouter/lib/mc/parser.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

class McParser {
 public:
  class ParserCallback {
   public:
    virtual ~ParserCallback() {};

    /**
     * We fully parsed an umbrella message and want to call RequestReady or
     * ReplyReady callback.
     *
     * @param umMsgInfo
     * @param header      Pointer to a contigous block of headerSize bytes
     * @param headerSize
     * @param body        Pointer to a contigous block of bodySize bytes,
     *                    must point inside bodyBuffer.
     * @param bodySize
     * @param bodyBuffer  Cloneable buffer that holds body bytes.
     * @return            False on any parse errors.
     */
    virtual bool umMessageReady(const UmbrellaMessageInfo& info,
                                const uint8_t* header,
                                const uint8_t* body,
                                const folly::IOBuf& bodyBuffer) = 0;

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

  McParser(ParserCallback& cb,
           size_t requestsPerRead,
           size_t minBufferSize,
           size_t maxBufferSize);

  ~McParser();

  mc_protocol_t protocol() const {
    return protocol_;
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

  void reportMsgRead() {
    ++parsedMessages_;
  }

  void reset();
 private:
  bool seenFirstByte_{false};
  bool outOfOrder_{false};

  /* We shrink the read buffer if we grow it beyond bufferSize_ */
  bool bufferShrinkRequired_{false};

  mc_protocol_t protocol_{mc_unknown_protocol};

  ParserCallback& callback_;
  size_t messagesPerRead_{0};
  size_t minBufferSize_{256};
  size_t maxBufferSize_{4096};
  size_t bufferSize_{4096};

  size_t readBytes_{0};
  size_t parsedMessages_{0};
  double bytesPerRequest_{0.0};

  folly::IOBuf readBuffer_;

  /**
   * If we've read an umbrella header, this will contain header/body sizes.
   */
  UmbrellaMessageInfo umMsgInfo_;

  /**
   * If this is nonempty, we're currently reading in umbrella message body.
   * We know we're done when this has as much data as umMsgInfo_.body_size
   */
  std::unique_ptr<folly::IOBuf> umBodyBuffer_;

  bool readUmbrellaData();

  /*
   * Determine the protocol by looking at the first byte
   */
  mc_protocol_t determineProtocol(uint8_t first_byte) {
    switch (first_byte) {
      case ENTRY_LIST_MAGIC_BYTE:
      case kCaretMagicByte:
        return mc_umbrella_protocol;
      default:
        return mc_ascii_protocol;
    }
  }

  void recalculateBufferSize(size_t read);

  /**
   * Shrink all buffers used if possible to reduce memory footprint.
   */
  void shrinkBuffers();
};

}}  // facebook::memcache
