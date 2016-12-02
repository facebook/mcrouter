/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class Proxy;

template <class RouterInfo, class Request>
class ProxyRequestContextTyped
    : public ProxyRequestContextWithInfo<RouterInfo> {
 public:
  using Type = ProxyRequestContextTyped<RouterInfo, Request>;
  /**
   * Sends the reply for this proxy request.
   * @param newReply the message that we are sending out as the reply
   *   for the request we are currently handling
   */
  void sendReply(ReplyT<Request>&& reply);

  /**
   * DEPRECATED. Convenience method, that constructs reply and calls
   * non-template method.
   *
   * WARNING: This function can be dangerous with new typed requests.
   * For typed requests,
   *   ctx->sendReply(mc_res_local_error, "Error message")
   * does the right thing, while
   *   ctx->sendReply(mc_res_found, "value")
   * does the wrong thing.
   */
  template <class... Args>
  void sendReply(Args&&... args) {
    sendReply(ReplyT<Request>(std::forward<Args>(args)...));
  }

  void startProcessing() override final;

  const ProxyConfig<RouterInfo>& proxyConfig() const {
    assert(!this->recording());
    return *config_;
  }

  ProxyRoute<RouterInfo>& proxyRoute() const {
    assert(!this->recording());
    return config_->proxyRoute();
  }

  /**
   * Internally converts the context into one ready to route.
   * Config pointer is saved to keep the config alive, and
   * ownership is changed to shared so that all subrequests
   * keep track of this context.
   */
  static std::shared_ptr<Type> process(
      std::unique_ptr<Type> preq,
      std::shared_ptr<const ProxyConfig<RouterInfo>> config);

 protected:
  ProxyRequestContextTyped(
      Proxy<RouterInfo>& pr,
      const Request& req,
      ProxyRequestPriority priority__)
      : ProxyRequestContextWithInfo<RouterInfo>(pr, priority__),
        proxy_(pr),
        req_(&req) {}

  Proxy<RouterInfo>& proxy_;

  std::shared_ptr<const ProxyConfig<RouterInfo>> config_;

  // It's guaranteed to point to an existing request until we call user callback
  // (i.e. replied_ changes to true), after that it's nullptr.
  const Request* req_;

  virtual void sendReplyImpl(ReplyT<Request>&& reply) = 0;

 private:
  friend class Proxy<RouterInfo>;
};

// TODO(@aap): Template by RouterInfo and kill McrouterRouterInfo
/**
 * Creates a new proxy request context
 */
template <class Request, class F>
std::unique_ptr<ProxyRequestContextTyped<McrouterRouterInfo, Request>>
createProxyRequestContext(
    Proxy<McrouterRouterInfo>& pr,
    const Request& req,
    F&& f,
    ProxyRequestPriority priority = ProxyRequestPriority::kCritical);

} // mcrouter
} // memcache
} // facebook

#include "ProxyRequestContextTyped-inl.h"
