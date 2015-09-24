/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyThread.h"

#include <folly/io/async/EventBase.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/MessageQueue.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ThreadUtil.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyThread::ProxyThread(McrouterInstance& router)
    : evb_(/* enableTimeMeasurement */ false),
      proxy_(proxy_t::createProxy(router, evb_)) {
}

void ProxyThread::spawn() {
  CHECK(state_.exchange(State::RUNNING) == State::STOPPED);
  thread_ = std::thread([this] () { proxyThreadRun(); });
}

void ProxyThread::stopAndJoin() noexcept {
  if (thread_.joinable() && proxy_->router().pid() == getpid()) {
    CHECK(state_.exchange(State::STOPPING) == State::RUNNING);
    proxy_->sendMessage(ProxyMessage::Type::SHUTDOWN, nullptr);
    CHECK(state_.exchange(State::STOPPED) == State::STOPPING);
    evb_.terminateLoopSoon();
    thread_.join();
  }
}

void ProxyThread::proxyThreadRun() {
  mcrouterSetThisThreadName(proxy_->router().opts(), "mcrpxy");

  while (state_ == State::RUNNING || proxy_->fiberManager.hasTasks()) {
    evb_.loopOnce();
    proxy_->drainMessageQueue();
  }

  while (state_ != State::STOPPED) {
    evb_.loopOnce();
  }

  /* make sure proxy is deleted on the proxy thread */
  proxy_.reset();
}

}}}  // facebook::memcache::mcrouter
