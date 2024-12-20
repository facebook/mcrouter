/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/ServiceInfo.h"
#include "mcrouter/mcrouter_sr_deps.h"

namespace facebook::memcache::mcrouter {

template class ServiceInfo<facebook::memcache::MemcacheRouterInfo>;

namespace detail {
bool srHostInfoPtrFuncRouteHandlesCommandDispatcher(
    const HostInfoPtr& host,
    std::string& tree,
    const int level) {
  bool haveHost = (host != nullptr);
  tree.append(
      std::string(level, ' ') +
      "host: " + (haveHost ? host->location().getIp() : "unknown") + " port:" +
      (haveHost ? folly::to<std::string>(host->location().getPort())
                : "unknown") +
      '\n');
  return false;
}
} // namespace detail
} // namespace facebook::memcache::mcrouter
