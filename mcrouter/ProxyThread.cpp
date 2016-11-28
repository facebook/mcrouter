/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyThread.h"

#include <folly/io/async/EventBase.h>

#include "mcrouter/McrouterInstance.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ThreadUtil.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/MessageQueue.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {
class MessageQueueDrainCallback : public folly::EventBase::LoopCallback {
 public:
   MessageQueueDrainCallback(folly::EventBase& evb, Proxy& proxy) :
    evb_(evb), proxy_(proxy) {
      evb_.runBeforeLoop(this);
    }

  void runLoopCallback() noexcept override {
    proxy_.drainMessageQueue();
    evb_.runBeforeLoop(this);
  }

 private:
  folly::EventBase& evb_;
  Proxy& proxy_;
};
}

ProxyThread::ProxyThread(McrouterInstance& router, size_t id)
    : evb_(folly::make_unique<folly::EventBase>(
          /* enableTimeMeasurement */ false)),
      proxy_(Proxy::createProxy(router, *evb_, id)),
      evbRef_(*evb_),
      proxyRef_(*proxy_) {}

void ProxyThread::spawn() {
  CHECK(!thread_.joinable());
  thread_ = std::thread([
      evb = std::move(evb_), proxy = std::move(proxy_) ]() mutable {
    proxyThreadRun(std::move(evb), std::move(proxy));
  });
}

void ProxyThread::stopAndJoin() noexcept {
  if (thread_.joinable() && proxyRef_.router().pid() == getpid()) {
    evbRef_.terminateLoopSoon();
    thread_.join();
  }
}

void ProxyThread::proxyThreadRun(
    std::unique_ptr<folly::EventBase> evb,
    Proxy::Pointer proxy) {
  mcrouterSetThisThreadName(proxy->router().opts(), "mcrpxy");

  new MessageQueueDrainCallback(*evb, *proxy);

  evb->loopForever();
  evb.reset();

  /* make sure proxy is deleted on the proxy thread */
  proxy.reset();
}

}}}  // facebook::memcache::mcrouter
