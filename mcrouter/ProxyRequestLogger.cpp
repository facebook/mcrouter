/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/ProxyRequestLogger.h"

#include "mcrouter/lib/McReply.h"
#include "mcrouter/McrouterFiberContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

#define REQUEST_CLASS_ERROR_STATS(proxy, ERROR, reqClass)                      \
    do {                                                                       \
      if (reqClass.isNormal()) {                                               \
        stat_incr(proxy->stats, result_ ## ERROR ## _stat, 1);                 \
        stat_incr(proxy->stats, result_ ## ERROR ## _count_stat, 1);           \
      }                                                                        \
      stat_incr(proxy->stats, result_ ## ERROR ## _all_stat, 1);               \
      stat_incr(proxy->stats, result_ ## ERROR ## _all_count_stat, 1);         \
    } while(0)

void ProxyRequestLogger::logError(const mc_res_t result) {
  const auto reqClass = fiber_local::getRequestClass();
  if (isErrorResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, error, reqClass);
  }
  if (isConnectErrorResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, connect_error, reqClass);
  }
  if (isConnectTimeoutResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, connect_timeout, reqClass);
  }
  if (isDataTimeoutResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, data_timeout, reqClass);
  }
  if (isRedirectResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, busy, reqClass);
  }
  if (isTkoResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, tko, reqClass);
  }
  if (isLocalErrorResult(result)) {
    REQUEST_CLASS_ERROR_STATS(proxy_, local_error, reqClass);
  }
}

}}} // facebook::memcache::mcrouter
