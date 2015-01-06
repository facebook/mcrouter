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

#include <memory>

#include "mcrouter/lib/mc/parser.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

class McRequest;

/**
 * A class for serializing memcache requests into iovs.
 */
class McSerializedRequest {
 public:
  enum class Result {
    OK,
    BAD_KEY,
    ERROR
  };

  /**
   * Creates serialized representation of request for a given mc_protocol.
   *
   * @param req  request to serialize, caller is responsible to keep it alive
   *             for the whole lifecycle of this serialized request.
   */
  template<int Op>
  McSerializedRequest(const McRequest& req, McOperation<Op>, size_t reqId,
                      mc_protocol_t protocol);
  ~McSerializedRequest();

  McSerializedRequest(const McSerializedRequest&) = delete;
  McSerializedRequest& operator=(const McSerializedRequest&) = delete;

  Result serializationResult() const;
  size_t getIovsCount() { return iovsCount_; }
  struct iovec* getIovs() { return iovsBegin_; }

 private:
  static const size_t kMaxIovs = 20;
  /**
   * Temporary structure for holding iovecs for ascii request.
   * Will be replaced by AsciiSerializedMessage
   */
  struct AsciiSerializedRequest {
    struct iovec iovs[kMaxIovs];
    std::unique_ptr<char[]> asciiBuffer;
  };

  union {
    AsciiSerializedRequest asciiRequest_;
    UmbrellaSerializedMessage umbrellaMessage_;
  };

  struct iovec* iovsBegin_{nullptr};
  size_t iovsCount_{0};
  mc_protocol_t protocol_{mc_unknown_protocol};
  Result result_{Result::OK};

  void serializeMcMsgAscii(const McMsgRef& req);
};

}} // facebook::memcache

#include "McSerializedRequest-inl.h"
