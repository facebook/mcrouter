/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/EventBase.h>

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ThreadUtil.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/MessageQueue.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouterInfo>
ProxyThread<RouterInfo>::ProxyThread(
    CarbonRouterInstanceBase& router,
    size_t id)
    : evb_(folly::make_unique<folly::EventBase>(
          /* enableTimeMeasurement */ false)),
      proxy_(Proxy<RouterInfo>::createProxy(router, *evb_, id)),
      evbRef_(*evb_),
      proxyRef_(*proxy_) {}

template <class RouterInfo>
void ProxyThread<RouterInfo>::spawn() {
  CHECK(!thread_.joinable());
  thread_ = std::thread([
      evb = std::move(evb_), proxy = std::move(proxy_) ]() mutable {
    proxyThreadRun(std::move(evb), std::move(proxy));
  });
}

template <class RouterInfo>
void ProxyThread<RouterInfo>::stopAndJoin() noexcept {
  if (thread_.joinable() && proxyRef_.router().pid() == getpid()) {
    evbRef_.terminateLoopSoon();
    thread_.join();
  }
}

template <class RouterInfo>
void ProxyThread<RouterInfo>::proxyThreadRun(
    std::unique_ptr<folly::EventBase> evb,
    typename Proxy<RouterInfo>::Pointer proxy) {
  mcrouterSetThisThreadName(proxy->router().opts(), "mcrpxy");

  evb->runOnDestruction(new folly::EventBase::FunctionLoopCallback(
      [proxy = std::move(proxy)]() mutable {
        /* make sure proxy is deleted on the proxy thread */
        proxy.reset();
      }));

  evb->loopForever();
  evb.reset();
}
}}}  // facebook::memcache::mcrouter
