/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/network/ThriftMessageTraits.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/RequestLoggerContext.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

#define REQUEST_CLASS_STATS(proxy, OP, SUFFIX, reqClass)                       \
    do {                                                                       \
      if (reqClass.isNormal()) {                                               \
        stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _stat, 1);         \
        stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _count_stat, 1);   \
      }                                                                        \
      stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _all_stat, 1);       \
      stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _all_count_stat, 1); \
    } while(0)

namespace detail {

template <class Request>
inline void logOutlier(proxy_t& proxy, GetLikeT<Request> = 0) {
  REQUEST_CLASS_STATS(proxy, get, outlier, fiber_local::getRequestClass());
}

template <class Request>
inline void logOutlier(proxy_t& proxy, UpdateLikeT<Request> = 0) {
  REQUEST_CLASS_STATS(proxy, set, outlier, fiber_local::getRequestClass());
}

template <class Request>
inline void logOutlier(proxy_t& proxy, DeleteLikeT<Request> = 0) {
  REQUEST_CLASS_STATS(proxy, delete, outlier, fiber_local::getRequestClass());
}

template <class Request>
inline void logOutlier(proxy_t& proxy, OtherThanT<Request,
                                                  GetLike<>,
                                                  UpdateLike<>,
                                                  DeleteLike<>> = 0) {
  REQUEST_CLASS_STATS(proxy, other, outlier, fiber_local::getRequestClass());
}

template <class Request>
inline void logRequestClass(proxy_t& proxy) {
  auto reqClass = fiber_local::getRequestClass();
  auto operation = Request::OpType::mc_op;

  switch (operation) {
    case mc_op_get:
      REQUEST_CLASS_STATS(proxy, get, out, reqClass);
      break;
    case mc_op_metaget:
      REQUEST_CLASS_STATS(proxy, meta, out, reqClass);
      break;
    case mc_op_add:
      REQUEST_CLASS_STATS(proxy, add, out, reqClass);
      break;
    case mc_op_cas:
      REQUEST_CLASS_STATS(proxy, cas, out, reqClass);
      break;
    case mc_op_gets:
      REQUEST_CLASS_STATS(proxy, gets, out, reqClass);
      break;
    case mc_op_replace:
      REQUEST_CLASS_STATS(proxy, replace, out, reqClass);
      break;
    case mc_op_set:
      REQUEST_CLASS_STATS(proxy, set, out, reqClass);
      break;
    case mc_op_incr:
      REQUEST_CLASS_STATS(proxy, incr, out, reqClass);
      break;
    case mc_op_decr:
      REQUEST_CLASS_STATS(proxy, decr, out, reqClass);
      break;
    case mc_op_delete:
      REQUEST_CLASS_STATS(proxy, delete, out, reqClass);
      break;
    case mc_op_lease_set:
      REQUEST_CLASS_STATS(proxy, lease_set, out, reqClass);
      break;
    case mc_op_lease_get:
      REQUEST_CLASS_STATS(proxy, lease_get, out, reqClass);
      break;
    default:
      REQUEST_CLASS_STATS(proxy, other, out, reqClass);
      break;
  }
}

} // detail

template <class Request>
void ProxyRequestLogger::log(const RequestLoggerContext& loggerContext) {
  const auto durationUs = loggerContext.endTimeUs - loggerContext.startTimeUs;
  bool isOutlier =
      proxy_->getRouterOptions().logging_rtt_outlier_threshold_us > 0 &&
      durationUs >= proxy_->getRouterOptions().logging_rtt_outlier_threshold_us;

  logError(loggerContext.replyResult);
  detail::logRequestClass<Request>(*proxy_);
  proxy_->durationUs.insertSample(durationUs);

  if (isOutlier) {
    detail::logOutlier<Request>(*proxy_);
  }
}

}}}  // facebook::memcache::mcrouter
