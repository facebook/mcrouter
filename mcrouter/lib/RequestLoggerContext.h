/*
 *  Copyright (c) 2016-present, Facebook, Inc.
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

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"

namespace folly {
class IOBuf;
} // folly

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
      const mc_res_t replyResult_,
      const ReplyStatsContext replyStatsContext_)
      : strippedRoutingPrefix(strippedRoutingPrefix_),
        requestClass(requestClass_),
        poolName(poolName_),
        ap(ap_),
        startTimeUs(startTimeUs_),
        endTimeUs(endTimeUs_),
        replyResult(replyResult_),
        replyStatsContext(replyStatsContext_) {}

  RequestLoggerContext(const RequestLoggerContext&) = delete;
  RequestLoggerContext& operator=(const RequestLoggerContext&) = delete;

  const folly::StringPiece strippedRoutingPrefix;
  const RequestClass requestClass;
  const folly::StringPiece poolName;
  const AccessPoint& ap;
  const int64_t startTimeUs;
  const int64_t endTimeUs;
  const mc_res_t replyResult;
  const ReplyStatsContext replyStatsContext;
};

} // mcrouter
} // memcache
} // facebook
