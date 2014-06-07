/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>

#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/mc/parser.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/mc/umbrella_protocol.h"

namespace folly {
class IOBuf;
}

namespace facebook { namespace memcache {
/**
 * A class for memcache protocol serialization.
 */
class McProtocolSerializer {
 public:
  class RequestContext;

  enum class Result {
    OK,
    BAD_KEY,
    ERROR
  };

  /**
   * @param onReady  callback to call for each parsed message.
   * @param onError  callback to call when parser failed to parse message.
   */
  McProtocolSerializer(mc_protocol_t protocol,
                       std::function<void(uint64_t, McMsgRef&&)> onReady,
                       std::function<void(parser_error_t)> onError);

  ~McProtocolSerializer();

  McProtocolSerializer& operator=(McProtocolSerializer&&);

  McProtocolSerializer(const McProtocolSerializer&) = delete;
  McProtocolSerializer& operator=(const McProtocolSerializer&) = delete;

  /**
   * Serialize given request into iovectors.
   *
   * @param ctx  context object for this request. It will be used as a storage
   *             for serialized data.
   */
  Result serialize(const McMsgRef& req, size_t reqId,
                   RequestContext& ctx) const;

  /**
   * Process incoming data. Will call onReady for each parsed message.
   */
  void readData(std::unique_ptr<folly::IOBuf>&& data);

 private:
  mc_protocol_t protocol_;
  mc_parser_t parser_;

  std::function<void(uint64_t, McMsgRef&&)> onReady_;
  std::function<void(parser_error_t)> onError_;

  // mc_paser_callbacks
  static void parserMsgReady(void* context, uint64_t reqId, mc_msg_t* msg);
  static void parserParseError(void* context, parser_error_t error);
  static void parserParseErrorSilent(void* context, parser_error_t error);
};

/**
 * A class for storing internal data used by serializer on per request basis,
 * as well provides interface to obtain serialized result.
 */
class McProtocolSerializer::RequestContext {
  friend class McProtocolSerializer;
 public:
  RequestContext();
  ~RequestContext();

  size_t getIovsCount() { return iovsCount_; }
  struct iovec* getIovs() { return iovs_; }

 private:
  static const size_t kMaxIovs = 20;
  struct iovec iovs_[kMaxIovs];
  size_t iovsCount_;
  um_backing_msg_t umBackingMsg_;
  std::unique_ptr<char[]> asciiBuffer_;
  McMsgRef msg_;

  void serializeMcMsgAscii(const McMsgRef& req);
  void serializeMcMsgUmbrella(const McMsgRef& req, size_t reqId);
};

}} // facebook::memcache
