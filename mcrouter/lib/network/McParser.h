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

namespace facebook { namespace memcache {

class McParser {
 public:

  class ServerParseCallback {
   public:
    virtual ~ServerParseCallback() {}

    /**
     * Called on every new request.
     * @param result  Some requests come in with a pre-filled result,
     *                one example is mc_res_bad_key.
     *                ParseError is not appropriate in this case since
     *                we usually can keep parsing, but we need to notify
     *                that the request is invalid.
     * @param noreply  If true, the server should not serialize and send
     *                 a reply for this request.
     */
    virtual void requestReady(McRequest&& req,
                              mc_op_t operation,
                              uint64_t reqid,
                              mc_res_t result,
                              bool noreply) = 0;

    /**
     * Called on fatal parse error (the stream should normally be closed)
     */
    virtual void parseError(McReply errorReply) = 0;
  };

  class ClientParseCallback {
   public:
    virtual ~ClientParseCallback() {}

    /**
     * Called on every new reply.
     */
    virtual void replyReady(McReply reply,
                            mc_op_t operation,
                            uint64_t reqid) = 0;

    /**
     * Called on fatal parse error (the stream should normally be closed)
     */
    virtual void parseError(McReply errorReply) = 0;
  };

  McParser(ServerParseCallback* cb,
           size_t requestsPerRead,
           size_t minBufferSize,
           size_t maxBufferSize);

  McParser(ClientParseCallback* cb,
           size_t repliesPerRead,
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

 private:
  bool seenFirstByte_{false};
  bool outOfOrder_{false};

  /* We shrink the read buffer if we grow it beyond bufferSize_ */
  bool bufferShrinkRequired_{false};

  mc_protocol_t protocol_{mc_unknown_protocol};
  mc_parser_t mcParser_;

  enum class ParserType {
    SERVER,
    CLIENT
  };

  ParserType type_;

  union {
    ServerParseCallback* serverParseCallback_;
    ClientParseCallback* clientParseCallback_;
  };

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
  um_message_info_t umMsgInfo_{0};

  /**
   * If this is nonempty, we're currently reading in umbrella message body.
   * We know we're done when this has as much data as umMsgInfo_.body_size
   */
  std::unique_ptr<folly::IOBuf> umBodyBuffer_;

  /**
   * We fully parsed an umbrella message and want to call RequestReady or
   * ReplyReady callback.
   * umMsgInfo_ must be filled out.
   *
   * @param header      Pointer to a contigous block of header_size bytes
   * @param body        Pointer to a contigous block of body_size bytes,
   *                    must point inside bodyBuffer.
   * @param bodyBuffer  Cloneable buffer that holds body bytes.
   * @return            False on any parse errors.
   */
  bool umMessageReady(
    const uint8_t* header,
    const uint8_t* body,
    const folly::IOBuf& bodyBuffer);

  bool readUmbrellaData();

  void requestReadyHelper(McRequest&& req, mc_op_t operation, uint64_t reqid,
                          mc_res_t result, bool noreply);
  void replyReadyHelper(McReply reply, mc_op_t operation, uint64_t reqid);
  void recalculateBufferSize(size_t read);
  void errorHelper(McReply reply);

  /**
   * Shrink all buffers used if possible to reduce memory footprint.
   */
  void shrinkBuffers();

  /* mc_parser callbacks */
  void msgReady(McMsgRef msg, uint64_t reqid);
  void parseError(parser_error_t error);
  static void parserMsgReady(void* context, uint64_t reqid, mc_msg_t* req);
  static void parserParseError(void* context, parser_error_t error);
};

}}  // facebook::memcache
