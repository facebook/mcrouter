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
      proxy_(new proxy_t(router, evb_)) {
}

bool ProxyThread::spawn() {
  return spawnThread(&thread_handle,
                     &thread_stack,
                     proxyThreadRunHandler, this);
}

void ProxyThread::stopAndJoin() {
  if (thread_handle && proxy_->router().pid() == getpid()) {
    proxy_->sendMessage(ProxyMessage::Type::SHUTDOWN, nullptr);
    {
      std::unique_lock<std::mutex> lk(mux);
      isSafeToDeleteProxy = true;
    }
    cv.notify_all();
    pthread_join(thread_handle, nullptr);
  }
  if (thread_stack) {
    free(thread_stack);
  }
}

void ProxyThread::proxyThreadRun() {
  mcrouterSetThreadName(pthread_self(), proxy_->router().opts(), "mcrpxy");

  while (!proxy_->router().shutdownStarted() ||
         proxy_->fiberManager.hasTasks()) {
    proxy_->eventBase().loopOnce();
    proxy_->drainMessageQueue();
  }

  // Delete the proxy from the proxy thread so that the clients get
  // deleted from the same thread where they were created.
  std::unique_lock<std::mutex> lk(mux);
  // This is to avoid a race condition where proxy is deleted
  // before the call to stopAndJoin is made.
  cv.wait(lk,
    [this]() {
      return this->isSafeToDeleteProxy;
    });

  /* make sure proxy is deleted on the proxy thread */
  proxy_.reset();
}

void *ProxyThread::proxyThreadRunHandler(void *arg) {
  auto proxyThread = reinterpret_cast<ProxyThread*>(arg);
  proxyThread->proxyThreadRun();
  return nullptr;
}

}}}  // facebook::memcache::mcrouter
