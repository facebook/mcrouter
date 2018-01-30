/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/ProxyRequestContextTyped.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {
template <class Request>
void bumpCarbonRouterClientStats(
    CacheClientStats& stats,
    const Request& req,
    const ReplyT<Request>& reply,
    carbon::GetLikeT<Request> = 0) {
  auto replyBytes = carbon::valuePtrUnsafe(reply)
      ? carbon::valuePtrUnsafe(reply)->computeChainDataLength()
      : 0;
  stats.recordFetchRequest(req.key().fullKey().size(), replyBytes);
}

template <class Request>
void bumpCarbonRouterClientStats(
    CacheClientStats& stats,
    const Request& req,
    const ReplyT<Request>&,
    carbon::UpdateLikeT<Request> = 0) {
  auto valueBytes = carbon::valuePtrUnsafe(req)
      ? carbon::valuePtrUnsafe(req)->computeChainDataLength()
      : 0;
  stats.recordUpdateRequest(req.key().fullKey().size(), valueBytes);
}

template <class Request>
void bumpCarbonRouterClientStats(
    CacheClientStats& stats,
    const Request& req,
    const ReplyT<Request>&,
    carbon::ArithmeticLikeT<Request> = 0) {
  stats.recordUpdateRequest(req.key().fullKey().size(), 0);
}

template <class Request>
void bumpCarbonRouterClientStats(
    CacheClientStats& stats,
    const Request& req,
    const ReplyT<Request>&,
    carbon::DeleteLikeT<Request> = 0) {
  stats.recordInvalidateRequest(req.key().fullKey().size());
}

template <class Request>
void bumpCarbonRouterClientStats(
    CacheClientStats&,
    const Request&,
    const ReplyT<Request>&,
    carbon::OtherThanT<
        Request,
        carbon::GetLike<>,
        carbon::UpdateLike<>,
        carbon::ArithmeticLike<>,
        carbon::DeleteLike<>> = 0) {
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

template <class RouterInfo>
template <class Request, class F>
bool CarbonRouterClient<RouterInfo>::send(
    const Request& req,
    F&& callback,
    folly::StringPiece ipAddr) {
  auto makePreq = [this, ipAddr, &req, &callback] {
    auto preq = createProxyRequestContext(*proxy_, req, [
      this,
      cb = std::forward<F>(callback)
    ](const Request& request, ReplyT<Request>&& reply) mutable {
      detail::bumpCarbonRouterClientStats(stats_, request, reply);
      if (disconnected_) {
        // "Cancelled" reply.
        cb(request, ReplyT<Request>(mc_res_unknown));
      } else {
        cb(request, std::move(reply));
      }
    });

    preq->setRequester(self_);
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

template <class RouterInfo>
template <class F, class G>
bool CarbonRouterClient<RouterInfo>::sendMultiImpl(
    size_t nreqs,
    F&& makeNextPreq,
    G&& failRemaining) {
  auto router = router_.lock();
  if (!router) {
    return false;
  }

  if (maxOutstanding() == 0) {
    if (sameThread_) {
      for (size_t i = 0; i < nreqs; ++i) {
        sendSameThread(makeNextPreq());
      }
    } else {
      for (size_t i = 0; i < nreqs; ++i) {
        sendRemoteThread(makeNextPreq());
      }
    }
  } else if (maxOutstandingError()) {
    for (size_t begin = 0; begin < nreqs;) {
      auto end = begin +
          counting_sem_lazy_nonblocking(outstandingReqsSem(), nreqs - begin);
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
      n += counting_sem_lazy_wait(outstandingReqsSem(), nreqs - n);
      for (size_t j = i; j < n; ++j) {
        sendRemoteThread(makeNextPreq());
      }
      i = n;
    }
  }

  return true;
}

template <class RouterInfo>
template <class InputIt, class F>
bool CarbonRouterClient<RouterInfo>::send(
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
          detail::bumpCarbonRouterClientStats(stats_, request, reply);
          if (disconnected_) {
            // "Cancelled" reply.
            callback(request, ReplyT<Request>(mc_res_unknown));
          } else {
            callback(request, std::move(reply));
          }
        });

    preq->setRequester(self_);
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

template <class RouterInfo>
void CarbonRouterClient<RouterInfo>::sendRemoteThread(
    std::unique_ptr<ProxyRequestContext> req) {
  proxy_->messageQueue_->blockingWriteRelaxed(
      ProxyMessage::Type::REQUEST, req.release());
}

template <class RouterInfo>
void CarbonRouterClient<RouterInfo>::sendSameThread(
    std::unique_ptr<ProxyRequestContext> req) {
  proxy_->messageReady(ProxyMessage::Type::REQUEST, req.release());
}

template <class RouterInfo>
CarbonRouterClient<RouterInfo>::CarbonRouterClient(
    std::weak_ptr<CarbonRouterInstance<RouterInfo>> rtr,
    size_t maximumOutstanding,
    bool maximumOutstandingError,
    bool sameThread)
    : CarbonRouterClientBase(maximumOutstanding, maximumOutstandingError),
      router_(std::move(rtr)),
      sameThread_(sameThread) {
  if (auto router = router_.lock()) {
    proxy_ = router->getProxy(router->nextProxyIndex());
  }
}

template <class RouterInfo>
typename CarbonRouterClient<RouterInfo>::Pointer
CarbonRouterClient<RouterInfo>::create(
    std::weak_ptr<CarbonRouterInstance<RouterInfo>> router,
    size_t maximumOutstanding,
    bool maximumOutstandingError,
    bool sameThread) {
  auto client = new CarbonRouterClient<RouterInfo>(
      std::move(router),
      maximumOutstanding,
      maximumOutstandingError,
      sameThread);
  client->self_ = std::shared_ptr<CarbonRouterClient>(client);
  return Pointer(client);
}

template <class RouterInfo>
CarbonRouterClient<RouterInfo>::~CarbonRouterClient() {
  assert(disconnected_);
}

} // mcrouter
} // memcache
} // facebook
