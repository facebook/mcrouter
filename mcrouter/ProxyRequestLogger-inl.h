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
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

#define REQUEST_CLASS_STATS(proxy, OP, SUFFIX, reqClass)                       \
    do {                                                                       \
      if (reqClass.isNormal()) {                                               \
        stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _stat, 1);         \
        stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _count_stat, 1);   \
      }                                                                        \
      stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _all_stat, 1);       \
      stat_incr(proxy.stats, cmd_ ## OP ## _ ## SUFFIX ## _all_count_stat, 1); \
    } while(0)

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

// TODO(jmswen) Implement logRequestClass for custom Thrift requests
template <
  class Request,
  typename std::enable_if<IsCustomRequest<Request>::value, int>::type = 0>
inline void logRequestClass(proxy_t& proxy, const Request& request) {
}

template <
  class Request,
  typename std::enable_if<!IsCustomRequest<Request>::value, int>::type = 0>
inline void logRequestClass(proxy_t& proxy, const Request& request) {
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

#define REQUEST_CLASS_ERROR_STATS(proxy, ERROR, reqClass)                      \
    do {                                                                       \
      if (reqClass.isNormal()) {                                               \
        stat_incr(proxy->stats, result_ ## ERROR ## _stat, 1);                 \
        stat_incr(proxy->stats, result_ ## ERROR ## _count_stat, 1);           \
      }                                                                        \
      stat_incr(proxy->stats, result_ ## ERROR ## _all_stat, 1);               \
      stat_incr(proxy->stats, result_ ## ERROR ## _all_count_stat, 1);         \
    } while(0)

}

template <class Reply>
void ProxyRequestLogger::logError(const Reply& reply) {
  auto reqClass = fiber_local::getRequestClass();
  if (reply.isError()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, error, reqClass);
  }
  if (reply.isConnectError()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, connect_error, reqClass);
  }
  if (reply.isConnectTimeout()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, connect_timeout, reqClass);
  }
  if (reply.isDataTimeout()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, data_timeout, reqClass);
  }
  if (reply.isRedirect()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, busy, reqClass);
  }
  if (reply.isTko()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, tko, reqClass);
  }
  if (reply.isLocalError()) {
    REQUEST_CLASS_ERROR_STATS(proxy_, local_error, reqClass);
  }
}

template <class Request>
void ProxyRequestLogger::log(const Request& request,
                             const ReplyT<Request>& reply,
                             const int64_t startTimeUs,
                             const int64_t endTimeUs) {

  auto durationUs = endTimeUs - startTimeUs;
  bool isOutlier =
      proxy_->getRouterOptions().logging_rtt_outlier_threshold_us > 0 &&
      durationUs >= proxy_->getRouterOptions().logging_rtt_outlier_threshold_us;

  logError(reply);
  logRequestClass(*proxy_, request);
  proxy_->durationUs.insertSample(durationUs);

  if (isOutlier) {
    logOutlier<Request>(*proxy_);
  }
}

}}}  // facebook::memcache::mcrouter
