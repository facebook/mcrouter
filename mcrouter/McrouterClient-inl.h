/*
 *  Copyright (c) 2015, Facebook, Inc.
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
template <class Operation, class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Operation, Request>& reply,
                             Operation,
                             GetLikeT<Operation> = 0) {

  auto replyBytes = reply.value().computeChainDataLength();
  stats.recordFetchRequest(req.fullKey().size(), replyBytes);
}

template <class Operation, class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Operation, Request>& reply,
                             Operation,
                             UpdateLikeT<Operation> = 0) {

  auto valueBytes = req.value().computeChainDataLength();
  stats.recordUpdateRequest(req.fullKey().size(), valueBytes);
}

template <class Operation, class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Operation, Request>& reply,
                             Operation,
                             ArithmeticLikeT<Operation> = 0) {

  stats.recordUpdateRequest(req.fullKey().size(), 0);
}

template <class Operation, class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Operation, Request>& reply,
                             Operation,
                             DeleteLikeT<Operation> = 0) {

  stats.recordInvalidateRequest(req.fullKey().size());
}

template <class Operation, class Request>
void bumpMcrouterClientStats(CacheClientStats& stats,
                             const Request& req,
                             const ReplyT<Operation, Request>& reply,
                             Operation,
                             OtherThanT<Operation,
                                        GetLike<>,
                                        UpdateLike<>,
                                        ArithmeticLike<>,
                                        DeleteLike<>> = 0) {
  // We don't have any other operation specific stats.
}
} // detail

template <class Operation, class Request, class F>
bool McrouterClient::send(const Request& req,
                          Operation,
                          F&& callback,
                          folly::StringPiece ipAddr) {
  auto router = router_.lock();
  if (!router) {
    return false;
  }

  auto preq = createProxyRequestContext(
      *proxy_,
      req,
      Operation(),
      [this, cb = std::forward<F>(callback)](
          const Request& request, ReplyT<Operation, Request>&& reply) {
        detail::bumpMcrouterClientStats(stats_, request, reply, Operation());
        if (disconnected_) {
          // "Cancelled" reply.
          cb(ReplyT<Operation, Request>(mc_res_unknown));
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
  } else {
    auto r = counting_sem_lazy_wait(&outstandingReqsSem_, 1);
    (void)r; // so that `r` not be unused under `NDEBUG`
    assert(r == 1);
    sendRemoteThread(std::move(preq));
  }

  return true;
}
}
}
} // facebook::memcache::mcrouter
