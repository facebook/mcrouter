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
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/mc/umbrella_protocol.h"
#include "mcrouter/lib/McMsgRef.h"

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
   *             for the whole lifecycle of of serialized request.
   */
  McSerializedRequest(const McRequest& req, mc_op_t operation, size_t reqId,
                      mc_protocol_t protocol);
  ~McSerializedRequest();

  McSerializedRequest(const McSerializedRequest&) = delete;
  McSerializedRequest& operator=(const McSerializedRequest&) = delete;

  Result serializationResult() const;
  size_t getIovsCount() { return iovsCount_; }
  struct iovec* getIovs() { return iovs_; }

 private:
  static const size_t kMaxIovs = 20;
  struct iovec iovs_[kMaxIovs];
  size_t iovsCount_;
  mc_protocol_t protocol_;
  um_backing_msg_t umBackingMsg_;
  std::unique_ptr<char[]> asciiBuffer_;
  Result result_;

  void serializeMcMsgAscii(const McMsgRef& req);
  void serializeMcMsgUmbrella(const McMsgRef& req, size_t reqId);
};

}} // facebook::memcache
