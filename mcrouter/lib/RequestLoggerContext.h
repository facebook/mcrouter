/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

#include <folly/Range.h>

#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/McrouterFiberContext.h"

namespace folly {
class IOBuf;
} // folly

namespace facebook { namespace memcache {

struct AccessPoint;

namespace mcrouter {

struct RequestLoggerContext {
  template <class Request>
  RequestLoggerContext(const std::string& poolName_,
                       const AccessPoint& ap_,
                       folly::StringPiece strippedRoutingPrefix_,
                       const Request& request,
                       const ReplyT<Request>& reply,
                       const int64_t startTimeUs_,
                       const int64_t endTimeUs_)
    : fullKey(request.key().fullKey()),
      keyWithoutRoute(request.key().keyWithoutRoute()),
      routingKey(request.key().routingKey()),
      routingPrefix(request.key().routingPrefix()),
      strippedRoutingPrefix(strippedRoutingPrefix_),
      requestName(Request::name),
      requestValue(carbon::valuePtrUnsafe(request)),
      requestClass(fiber_local::getRequestClass()),
      routingKeyHash(request.key().routingKeyHash()),
      replyValue(carbon::valuePtrUnsafe(reply)),
      replyResult(reply.result()),
      replyFlags(reply.flags()),
      poolName(poolName_),
      ap(ap_),
      startTimeUs(startTimeUs_),
      endTimeUs(endTimeUs_) {}

  RequestLoggerContext(const RequestLoggerContext&) = delete;
  RequestLoggerContext& operator=(const RequestLoggerContext&) = delete;

  /* Request-specific data */
  const folly::StringPiece fullKey;
  const folly::StringPiece keyWithoutRoute;
  const folly::StringPiece routingKey;
  const folly::StringPiece routingPrefix;
  const folly::StringPiece strippedRoutingPrefix;
  const char* const requestName;
  const folly::IOBuf* requestValue;
  const RequestClass requestClass;
  const uint32_t routingKeyHash;

  /* Reply-specific data */
  const folly::IOBuf* replyValue;
  const mc_res_t replyResult;
  const uint32_t replyFlags;

  const std::string& poolName;
  const AccessPoint& ap;
  const int64_t startTimeUs;
  const int64_t endTimeUs;
};

}}} // facebook::memcache::mcrouter
