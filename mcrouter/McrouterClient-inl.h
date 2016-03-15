/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/ProxyRequestContext.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {
template <class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Request>& reply,
                             GetLikeT<Request> = 0) {

  auto replyBytes = reply.valuePtrUnsafe()
    ? reply.valuePtrUnsafe()->computeChainDataLength()
    : 0;
  stats.recordFetchRequest(req.fullKey().size(), replyBytes);
}

template <class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Request>& reply,
                             UpdateLikeT<Request> = 0) {

  auto valueBytes = req->get_value().computeChainDataLength();
  stats.recordUpdateRequest(req.fullKey().size(), valueBytes);
}

template <class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Request>& reply,
                             ArithmeticLikeT<Request> = 0) {

  stats.recordUpdateRequest(req.fullKey().size(), 0);
}

template <class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Request>& reply,
                             DeleteLikeT<Request> = 0) {

  stats.recordInvalidateRequest(req.fullKey().size());
}

template <class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Request>& reply,
                             OtherThanT<Request,
                                        GetLike<>,
                                        UpdateLike<>,
                                        ArithmeticLike<>,
                                        DeleteLike<>> = 0) {
  // We don't have any other operation specific stats.
}
} // detail

template <class Request, class F>
typename
  std::enable_if<!std::is_convertible<Request, const mcrouter_msg_t*>::value,
                 bool>::type
McrouterClient::send(const Request& req,
                     F&& callback,
                     folly::StringPiece ipAddr) {
  auto router = router_.lock();
  if (!router) {
    return false;
  }

  auto preq = createProxyRequestContext(
      *proxy_,
      req,
      [this, cb = std::forward<F>(callback)](
          const Request& request, ReplyT<Request>&& reply) {
        detail::bumpMcrouterClientStats(stats_, request, reply);
        if (disconnected_) {
          // "Cancelled" reply.
          cb(ReplyT<Request>(mc_res_unknown));
        } else {
          cb(std::move(reply));
        }
      });
  preq->requester_ = self_;
  if (!ipAddr.empty()) {
    preq->setUserIpAddress(ipAddr);
  }

  if (sameThread_) {
    sendSameThread(std::move(preq));
  } else if (maxOutstanding_ == 0) {
    sendRemoteThread(std::move(preq));
  } else if (maxOutstandingError_) {
    auto r = counting_sem_lazy_nonblocking(&outstandingReqsSem_, 1);
    if (r == 0) {
      callback(ReplyT<Request>(mc_res_local_error));
    } else {
      assert(r == 1);
      sendRemoteThread(std::move(preq));
    }
  } else {
    auto r = counting_sem_lazy_wait(&outstandingReqsSem_, 1);
    (void)r; // so that `r` not be unused under `NDEBUG`
    assert(r == 1);
    sendRemoteThread(std::move(preq));
  }

  return true;
}
} // mcrouter
} // memcache
} // facebook
