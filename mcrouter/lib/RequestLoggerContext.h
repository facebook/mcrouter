/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include <folly/Range.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/RpcStatsContext.h"

namespace folly {
class IOBuf;
} // namespace folly

namespace facebook {
namespace memcache {

struct AccessPoint;

namespace mcrouter {

struct RequestLoggerContext {
  RequestLoggerContext(
      const folly::StringPiece poolName_,
      const AccessPoint& ap_,
      folly::StringPiece strippedRoutingPrefix_,
      RequestClass requestClass_,
      const int64_t startTimeUs_,
      const int64_t endTimeUs_,
      const carbon::Result replyResult_,
      const RpcStatsContext rpcStatsContext_,
      const int64_t networkTransportTimeUs_,
      const std::vector<ExtraDataCallbackT>& extraDataCallbacks_)
      : strippedRoutingPrefix(strippedRoutingPrefix_),
        requestClass(requestClass_),
        poolName(poolName_),
        ap(ap_),
        startTimeUs(startTimeUs_),
        endTimeUs(endTimeUs_),
        replyResult(replyResult_),
        rpcStatsContext(rpcStatsContext_),
        networkTransportTimeUs(networkTransportTimeUs_),
        extraDataCallbacks(extraDataCallbacks_) {}

  RequestLoggerContext(const RequestLoggerContext&) = delete;
  RequestLoggerContext& operator=(const RequestLoggerContext&) = delete;

  const folly::StringPiece strippedRoutingPrefix;
  const RequestClass requestClass;
  const folly::StringPiece poolName;
  const AccessPoint& ap;
  const int64_t startTimeUs;
  const int64_t endTimeUs;
  const carbon::Result replyResult;
  const RpcStatsContext rpcStatsContext;
  const int64_t networkTransportTimeUs;
  const std::vector<ExtraDataCallbackT>& extraDataCallbacks;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
