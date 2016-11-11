/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/Proxy.h"
#include "mcrouter/lib/McKey.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

/**
 * Implementation class for storing the callback along with the context.
 */
template <class RouteHandleIf, class Request, class F>
class ProxyRequestContextTypedWithCallback
    : public ProxyRequestContextTyped<RouteHandleIf, Request> {
 public:
  ProxyRequestContextTypedWithCallback(
      Proxy& pr,
      const Request& req,
      F&& f,
      ProxyRequestPriority priority__)
      : ProxyRequestContextTyped<RouteHandleIf, Request>(pr, req, priority__),
        f_(std::forward<F>(f)) {}

 protected:
  void sendReplyImpl(ReplyT<Request>&& reply) override final {
    auto req = this->req_;
    fiber_local::runWithoutLocals(
        [this, req, &reply]() { f_(*req, std::move(reply)); });
  }

 private:
  F f_;
};

constexpr const char* kCommandNotSupportedStr = "Command not supported";

template <class RouteHandleIf, class Request>
bool precheckKey(ProxyRequestContextTyped<RouteHandleIf, Request>& preq,
                 const Request& req) {
  auto key = req.key().fullKey();
  auto err = isKeyValid(key);
  if (err != mc_req_err_valid) {
    ReplyT<Request> reply(mc_res_local_error);
    reply.message() = mc_req_err_to_string(err);
    preq.sendReply(std::move(reply));
    return false;
  }
  return true;
}

// Following methods validate the request and return true if it's correct,
// otherwise they reply it with error and return false;

template <class RouteHandleIf, class Request>
bool precheckRequest(ProxyRequestContextTyped<RouteHandleIf, Request>& preq,
                     const Request& req) {
  return precheckKey(preq, req);
}

template <class RouteHandleIf>
bool precheckRequest(
    ProxyRequestContextTyped<RouteHandleIf, McStatsRequest>&,
    const McStatsRequest&) {
  return true;
}

template <class RouteHandleIf>
bool precheckRequest(
    ProxyRequestContextTyped<RouteHandleIf, McVersionRequest>&,
    const McVersionRequest&) {
  return true;
}

template <class RouteHandleIf>
bool precheckRequest(
    ProxyRequestContextTyped<RouteHandleIf, McShutdownRequest>& preq,
    const McShutdownRequest&) {
  // Return error (pretend to not even understand the protocol)
  preq.sendReply(mc_res_bad_command);
  return false;
}

template <class RouteHandleIf>
bool precheckRequest(
    ProxyRequestContextTyped<RouteHandleIf, McFlushReRequest>& preq,
    const McFlushReRequest&) {
  // Return 'Not supported' message
  McFlushReReply reply(mc_res_local_error);
  reply.message() = kCommandNotSupportedStr;
  preq.sendReply(std::move(reply));
  return false;
}

template <class RouteHandleIf>
bool precheckRequest(
    ProxyRequestContextTyped<RouteHandleIf, McFlushAllRequest>& preq,
    const McFlushAllRequest&) {
  if (!preq.proxy().getRouterOptions().enable_flush_cmd) {
    McFlushAllReply reply(mc_res_local_error);
    reply.message() = "Command disabled";
    preq.sendReply(std::move(reply));
    return false;
  }
  return true;
}

} // detail

template <class RouteHandleIf, class Request>
void ProxyRequestContextTyped<RouteHandleIf, Request>::sendReply(
    ReplyT<Request>&& reply) {
  if (this->recording()) {
    return;
  }

  if (this->replied_) {
    return;
  }
  this->replied_ = true;
  auto result = reply.result();

  sendReplyImpl(std::move(reply));
  req_ = nullptr;

  proxy().stats().increment(request_replied_stat);
  proxy().stats().increment(request_replied_count_stat);
  if (mc_res_is_err(result)) {
    proxy().stats().increment(request_error_stat);
    proxy().stats().increment(request_error_count_stat);
  } else {
    proxy().stats().increment(request_success_stat);
    proxy().stats().increment(request_success_count_stat);
  }
}

template <class RouteHandleIf, class Request>
void ProxyRequestContextTyped<RouteHandleIf, Request>::startProcessing() {
  std::unique_ptr<ProxyRequestContextTyped<RouteHandleIf, Request>> self(this);

  if (!detail::precheckRequest(*this, *req_)) {
    return;
  }

  if (proxy().beingDestroyed()) {
    /* We can't process this, since 1) we destroyed the config already,
       and 2) the clients are winding down, so we wouldn't get any
       meaningful response back anyway. */
    LOG(ERROR) << "Outstanding request on a proxy that's being destroyed";
    sendReply(ReplyT<Request>(mc_res_unknown));
    return;
  }

  proxy().dispatchRequest(*req_, std::move(self));
}

template <class RouteHandleIf, class Request>
std::shared_ptr<ProxyRequestContextTyped<RouteHandleIf, Request>>
ProxyRequestContextTyped<RouteHandleIf, Request>::process(
    std::unique_ptr<Type> preq,
    std::shared_ptr<const ProxyConfig<RouteHandleIf>> config) {
  preq->config_ = std::move(config);
  return std::shared_ptr<Type>(
      preq.release(),
      /* Note: we want to delete on main context here since the destructor
         can do complicated things, like finalize stats entry and
         destroy a stale config.  There might not be enough stack space
         for these operations. */
      [](ProxyRequestContext* ctx) {
        folly::fibers::runInMainContext([ctx] { delete ctx; });
      });
}

template <class Request, class F>
std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
createProxyRequestContext(Proxy& pr,
                          const Request& req,
                          F&& f,
                          ProxyRequestPriority priority) {
  using Type = detail::
      ProxyRequestContextTypedWithCallback<McrouterRouteHandleIf, Request, F>;
  return folly::make_unique<Type>(pr, req, std::forward<F>(f), priority);
}

} // mcrouter
} // memcache
} // facebook
