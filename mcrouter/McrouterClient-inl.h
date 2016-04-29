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

template <class Request>
const Request& unwrapRequest(const Request& req) {
  return req;
}

template <class Request>
const Request& unwrapRequest(std::reference_wrapper<const Request>& req) {
  return req.get();
}

} // detail

template <class Request, class F>
typename
  std::enable_if<!std::is_convertible<Request, const mcrouter_msg_t*>::value,
                 bool>::type
McrouterClient::send(const Request& req,
                     F&& callback,
                     folly::StringPiece ipAddr) {
  auto makePreq = [this, ipAddr, &req, &callback] {
    auto preq = createProxyRequestContext(*proxy_, req, [
      this,
      cb = std::forward<F>(callback)
    ](const Request& request, ReplyT<Request>&& reply) mutable {
      detail::bumpMcrouterClientStats(stats_, request, reply);
      if (disconnected_) {
        // "Cancelled" reply.
        cb(request, ReplyT<Request>(mc_res_unknown));
      } else {
        cb(request, std::move(reply));
      }
    });

    preq->requester_ = self_;
    if (!ipAddr.empty()) {
      preq->setUserIpAddress(ipAddr);
    }
    return preq;
  };

  auto cancelRemaining = [&req, &callback]() {
    callback(req, ReplyT<Request>(mc_res_local_error));
  };

  return sendMultiImpl(1, makePreq, cancelRemaining);
}

template <class F, class G>
bool McrouterClient::sendMultiImpl(
    size_t nreqs,
    F&& makeNextPreq,
    G&& failRemaining) {
  auto router = router_.lock();
  if (!router) {
    return false;
  }

  if (maxOutstanding_ == 0) {
    if (sameThread_) {
      for (size_t i = 0; i < nreqs; ++i) {
        sendSameThread(makeNextPreq());
      }
    } else {
      for (size_t i = 0; i < nreqs; ++i) {
        sendRemoteThread(makeNextPreq());
      }
    }
  } else if (maxOutstandingError_) {
    for (size_t begin = 0; begin < nreqs;) {
      auto end = begin +
          counting_sem_lazy_nonblocking(&outstandingReqsSem_, nreqs - begin);
      if (begin == end) {
        failRemaining();
        break;
      }

      if (sameThread_) {
        for (size_t i = begin; i < end; i++) {
          sendSameThread(makeNextPreq());
        }
      } else {
        for (size_t i = begin; i < end; i++) {
          sendRemoteThread(makeNextPreq());
        }
      }

      begin = end;
    }
  } else {
    assert(!sameThread_);

    size_t i = 0;
    size_t n = 0;

    while (i < nreqs) {
      n += counting_sem_lazy_wait(&outstandingReqsSem_, nreqs - n);
      for (size_t j = i; j < n; ++j) {
        sendRemoteThread(makeNextPreq());
      }
      i = n;
    }
  }

  return true;
}

template <class InputIt, class F>
bool McrouterClient::send(
    InputIt begin,
    InputIt end,
    F&& callback,
    folly::StringPiece ipAddr) {
  using IterReference = typename std::iterator_traits<InputIt>::reference;
  using Request = typename std::decay<decltype(
      detail::unwrapRequest(std::declval<IterReference>()))>::type;

  auto makeNextPreq = [this, ipAddr, &callback, &begin]() {
    auto preq = createProxyRequestContext(
        *proxy_,
        detail::unwrapRequest(*begin),
        [this, callback](
            const Request& request, ReplyT<Request>&& reply) mutable {
          detail::bumpMcrouterClientStats(stats_, request, reply);
          if (disconnected_) {
            // "Cancelled" reply.
            callback(request, ReplyT<Request>(mc_res_unknown));
          } else {
            callback(request, std::move(reply));
          }
        });

    preq->requester_ = self_;
    if (!ipAddr.empty()) {
      preq->setUserIpAddress(ipAddr);
    }

    ++begin;
    return preq;
  };

  auto cancelRemaining = [&begin, &end, &callback]() {
    while (begin != end) {
      callback(
          detail::unwrapRequest(*begin), ReplyT<Request>(mc_res_local_error));
      ++begin;
    }
  };

  return sendMultiImpl(
      std::distance(begin, end),
      std::move(makeNextPreq),
      std::move(cancelRemaining));
}

} // mcrouter
} // memcache
} // facebook
