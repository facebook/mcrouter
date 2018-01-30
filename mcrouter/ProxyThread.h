/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

namespace facebook {
namespace memcache {
namespace mcrouter {

class CarbonRouterInstanceBase;

class ProxyThread {
 public:
  ProxyThread(const CarbonRouterInstanceBase& router, size_t id);

  /**
   * Stops the underlying proxy thread and joins it.
   * Does nothing if "spawn" was not called.
   * Should be called at most once per process, i.e. it's fine to call it
   * after fork - only parent process will join the thread.
   */
  void stopAndJoin() noexcept;

  folly::EventBase& getEventBase() const;

 private:
  folly::EventBaseThread thread_;
};
}
}
} // facebook::memcache::mcrouter

#include "ProxyThread-inl.h"
