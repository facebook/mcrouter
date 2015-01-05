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

#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyThread::ProxyThread(std::unique_ptr<proxy_t> pr)
    : proxy_(std::move(pr)),
      thread_handle(0),
      thread_stack(nullptr),
      isSafeToDeleteProxy(false) {
  proxy_->attachEventBase(&evb_);
}

int ProxyThread::spawn() {
  return spawn_thread(&thread_handle,
                      &thread_stack,
                      proxyThreadRunHandler, this,
                      proxy_->router->wantRealtimeThreads());
}

void ProxyThread::stopAndJoin() {
  if (thread_handle && proxy_->router->pid == getpid()) {
    FBI_ASSERT(proxy_->request_queue != nullptr);
    asox_queue_entry_t entry;
    entry.type = request_type_router_shutdown;
    entry.priority = 0;
    entry.data = nullptr;
    entry.nbytes = 0;
    asox_queue_enqueue(proxy_->request_queue, &entry);
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
  FBI_ASSERT(proxy_->router != nullptr);
  mcrouter_set_thread_name(pthread_self(), proxy_->router->opts, "mcrpxy");

  while (!proxy_->router->shutdownStarted()) {
    mcrouterLoopOnce(proxy_->eventBase);
  }

  while (proxy_->fiberManager.hasTasks()) {
    mcrouterLoopOnce(proxy_->eventBase);
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
