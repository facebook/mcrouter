/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include <cassert>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/ForEachPossibleClient.h"
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

} // namespace detail

template <class RouterInfo>
template <class Request, class F>
bool CarbonRouterClient<RouterInfo>::send(
    const Request& req,
    F&& callback,
    folly::StringPiece ipAddr) {
  auto makePreq = [this, ipAddr, &req, &callback]() mutable {
    return makeProxyRequestContext(req, std::forward<F>(callback), ipAddr);
  };

  auto cancelRemaining = [&req, &callback]() {
    callback(req, ReplyT<Request>(carbon::Result::LOCAL_ERROR));
  };

  return sendMultiImpl(1, std::move(makePreq), std::move(cancelRemaining));
}

template <class RouterInfo>
template <class F, class G>
bool CarbonRouterClient<RouterInfo>::sendMultiImpl(
    size_t nreqs,
    F&& makeNextPreq,
    G&& failRemaining) {
  auto router = router_.lock();
  if (UNLIKELY(!router)) {
    return false;
  }

  if (maxOutstanding() == 0) {
    if (mode_ == ThreadMode::SameThread) {
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

      if (mode_ == ThreadMode::SameThread) {
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
    assert(mode_ != ThreadMode::SameThread);

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
template <class Request>
Proxy<RouterInfo>* CarbonRouterClient<RouterInfo>::getProxy(
    const Request& req) const {
  if (mode_ == ThreadMode::AffinitizedRemoteThread) {
    return proxies_[findAffinitizedProxyIdx(req)];
  }
  return proxies_[proxyIdx_];
}

template <class RouterInfo>
template <class Request>
typename std::enable_if<
    ListContains<typename RouterInfo::RoutableRequests, Request>::value,
    uint64_t>::type
CarbonRouterClient<RouterInfo>::findAffinitizedProxyIdx(
    const Request& req) const {
  assert(mode_ == ThreadMode::AffinitizedRemoteThread);
  uint64_t hash = 0;
  foreachPossibleClient<Request>(
      *(proxies_[0]),
      req,
      [&hash](
          const memcache::mcrouter::PoolContext&,
          const memcache::AccessPoint& ap) mutable { hash = ap.getHash(); },
      /* spCallback */ nullptr,
      /* includeFailoverDestinations */ true,
      /* traverseEarlyExit */ true);
  /* Hash on ipv6 address */
  return hash % proxies_.size();
}

template <class RouterInfo>
template <class Request>
typename std::enable_if<
    !ListContains<typename RouterInfo::RoutableRequests, Request>::value,
    uint64_t>::type
CarbonRouterClient<RouterInfo>::findAffinitizedProxyIdx(
    const Request& /* unused */) const {
  assert(mode_ == ThreadMode::AffinitizedRemoteThread);
  return 0;
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
    auto proxyRequestContext = makeProxyRequestContext(
        detail::unwrapRequest(*begin), callback, ipAddr);
    ++begin;
    return proxyRequestContext;
  };

  auto cancelRemaining = [&begin, &end, &callback]() {
    while (begin != end) {
      callback(
          detail::unwrapRequest(*begin),
          ReplyT<Request>(carbon::Result::LOCAL_ERROR));
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
    std::unique_ptr<ProxyRequestContextWithInfo<RouterInfo>> req) {
  // Use the proxy saved in the ProxyRequestContext, as it may change
  // base on the ThreadMode. Get a reference to the Proxy first as
  // the unique pointer is released as part of the blockingWriteRelaxed
  // call.
  Proxy<RouterInfo>& pr = req->proxyWithRouterInfo();
  pr.messageQueue_->blockingWriteRelaxed(
      ProxyMessage::Type::REQUEST, req.release());
}

template <class RouterInfo>
void CarbonRouterClient<RouterInfo>::sendSameThread(
    std::unique_ptr<ProxyRequestContextWithInfo<RouterInfo>> req) {
  // We are guaranteed to be in the thread that owns proxies_[proxyIdx_]
  proxies_[proxyIdx_]->messageReady(ProxyMessage::Type::REQUEST, req.release());
}

template <class RouterInfo>
CarbonRouterClient<RouterInfo>::CarbonRouterClient(
    std::shared_ptr<CarbonRouterInstance<RouterInfo>> router,
    size_t maximumOutstanding,
    bool maximumOutstandingError,
    ThreadMode mode)
    : CarbonRouterClientBase(maximumOutstanding, maximumOutstandingError),
      router_(router),
      mode_(mode),
      proxies_(router->getProxies()) {
  proxyIdx_ = router->nextProxyIndex();
}

template <class RouterInfo>
typename CarbonRouterClient<RouterInfo>::Pointer
CarbonRouterClient<RouterInfo>::create(
    std::shared_ptr<CarbonRouterInstance<RouterInfo>> router,
    size_t maximumOutstanding,
    bool maximumOutstandingError,
    ThreadMode mode) {
  auto client = new CarbonRouterClient<RouterInfo>(
      std::move(router), maximumOutstanding, maximumOutstandingError, mode);
  client->self_ = std::shared_ptr<CarbonRouterClient>(client);
  return Pointer(client);
}

template <class RouterInfo>
CarbonRouterClient<RouterInfo>::~CarbonRouterClient() {
  assert(disconnected_);
}

template <class RouterInfo>
template <class Request, class CallbackFunc>
std::unique_ptr<ProxyRequestContextWithInfo<RouterInfo>>
CarbonRouterClient<RouterInfo>::makeProxyRequestContext(
    const Request& req,
    CallbackFunc&& callback,
    folly::StringPiece ipAddr) {
  auto proxyRequestContext = createProxyRequestContext(
      *getProxy(req),
      req,
      [this, cb = std::forward<CallbackFunc>(callback)](
          const Request& request, ReplyT<Request>&& reply) mutable {
        detail::bumpCarbonRouterClientStats(stats_, request, reply);
        if (disconnected_) {
          // "Cancelled" reply.
          cb(request, ReplyT<Request>(carbon::Result::UNKNOWN));
        } else {
          cb(request, std::move(reply));
        }
      });

  proxyRequestContext->setRequester(self_);
  if (!ipAddr.empty()) {
    proxyRequestContext->setUserIpAddress(ipAddr);
  }
  return proxyRequestContext;
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
