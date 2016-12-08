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

#include <atomic>
#include <memory>
#include <thread>

#include <folly/io/async/EventBase.h>

#include "mcrouter/Proxy.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

class CarbonRouterInstanceBase;

template <class RouterInfo>
class Proxy;

template <class RouterInfo>
class ProxyThread {
 public:
  ProxyThread(CarbonRouterInstanceBase& router, size_t id);

  /**
   * Stops the underlying proxy thread and joins it.
   * Does nothing if "spawn" was not called.
   * Should be called at most once per process, i.e. it's fine to call it
   * after fork - only parent process will join the thread.
   */
  void stopAndJoin() noexcept;

  /**
   * Spawns a new proxy thread for execution. Should be called at most once.
   *
   * @throws std::system_error  If failed to spawn thread
   */
  void spawn();

  Proxy<RouterInfo>& proxy() {
    return *proxy_;
  }
  folly::EventBase& eventBase() { return evb_; }

 private:
  folly::EventBase evb_;
  typename Proxy<RouterInfo>::Pointer proxy_;
  std::thread thread_;

  enum class State {
    RUNNING,
    STOPPING,
    STOPPED
  };
  std::atomic<State> state_{State::STOPPED};

  void stopAwriterThreads();
  void proxyThreadRun();
};

}}}  // facebook::memcache::mcrouter

#include "ProxyThread-inl.h"
