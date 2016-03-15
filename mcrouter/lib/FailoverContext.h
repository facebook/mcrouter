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

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/Operation.h"

namespace folly {
class IOBuf;
} // folly

namespace facebook { namespace memcache {

struct AccessPoint;

namespace mcrouter {

struct FailoverContext {
  template <class Request>
  FailoverContext(size_t numRetries_,
                  size_t maxRetries_,
                  const Request& request,
                  const ReplyT<Request>& normal,
                  const ReplyT<Request>& failover)
    : requestName(Request::name),
      fullKey(request.fullKey()),
      routingKey(request.routingKey()),
      keyWithoutRoute(request.keyWithoutRoute()),
      requestValue(request.valuePtrUnsafe()),
      normalValue(normal.valuePtrUnsafe()),
      normalDestination(normal.destination().get()),
      normalResult(normal.result()),
      failoverValue(failover.valuePtrUnsafe()),
      failoverDestination(failover.destination().get()),
      failoverResult(failover.result()),
      numRetries(numRetries_),
      maxRetries(maxRetries_) {}

  FailoverContext(const FailoverContext&) = delete;
  FailoverContext& operator=(const FailoverContext&) = delete;

  /* Request data */
  const folly::StringPiece requestName;
  const folly::StringPiece fullKey;
  const folly::StringPiece routingKey;
  const folly::StringPiece routingPrefix;
  const folly::StringPiece keyWithoutRoute;
  const folly::IOBuf* requestValue;

  /* Normal reply data */
  const folly::IOBuf* normalValue;
  const AccessPoint* normalDestination;
  const mc_res_t normalResult;

  /* Failover reply data */
  const folly::IOBuf* failoverValue;
  const AccessPoint* failoverDestination;
  const mc_res_t failoverResult;

  const size_t numRetries;
  const size_t maxRetries;
};

}}} // facebook::memcache::mcrouter
