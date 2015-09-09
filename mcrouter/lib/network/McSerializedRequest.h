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
#include "mcrouter/lib/network/AsciiSerialized.h"
#include "mcrouter/lib/network/CaretSerializedMessage.h"
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
  template <int Op>
  McSerializedRequest(const McRequest& req,
                      McOperation<Op>,
                      size_t reqId,
                      mc_protocol_t protocol,
                      bool useTyped);
  ~McSerializedRequest();

  McSerializedRequest(const McSerializedRequest&) = delete;
  McSerializedRequest& operator=(const McSerializedRequest&) = delete;

  Result serializationResult() const;
  size_t getIovsCount() const { return iovsCount_; }
  struct iovec* getIovs() const { return iovsBegin_; }

 private:
  static const size_t kMaxIovs = 20;

  union {
    AsciiSerializedRequest asciiRequest_;
    UmbrellaSerializedMessage umbrellaMessage_;
    CaretSerializedMessage caretRequest_;
  };

  struct iovec* iovsBegin_{nullptr};
  size_t iovsCount_{0};
  mc_protocol_t protocol_{mc_unknown_protocol};
  const bool useTyped_{false};
  Result result_{Result::OK};

  bool checkKeyLength(const folly::IOBuf& key);
};

}} // facebook::memcache

#include "McSerializedRequest-inl.h"
