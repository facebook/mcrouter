/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/ThreadUtil.h"
#include "mcrouter/config.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

inline ProxyThread::ProxyThread(
    const CarbonRouterInstanceBase& router,
    size_t /* id */) {
  thread_.start();
  getEventBase().runInEventBaseThreadAndWait(
      [&] { mcrouterSetThisThreadName(router.opts(), "mcrpxy"); });
}

inline void ProxyThread::stopAndJoin() noexcept {
  thread_.stop();
}

inline folly::EventBase& ProxyThread::getEventBase() const {
  CHECK(thread_.running());
  return *thread_.getEventBase();
}
}
}
} // facebook::memcache::mcrouter
