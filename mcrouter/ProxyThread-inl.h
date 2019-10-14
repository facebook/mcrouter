/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
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
} // namespace mcrouter
} // namespace memcache
} // namespace facebook
