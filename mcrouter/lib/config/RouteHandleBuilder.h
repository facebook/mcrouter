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

#include <memory>

namespace facebook {
namespace memcache {

template <
    class RouteHandleIf,
    template <typename... Ignored> class R,
    typename... RArgs,
    typename... Args>
std::shared_ptr<RouteHandleIf> makeRouteHandle(Args&&... args) {
  return std::make_shared<
      typename RouteHandleIf::template Impl<R<RouteHandleIf, RArgs...>>>(
      std::forward<Args>(args)...);
}

template <
    class RouterInfo,
    template <typename... Ignored> class R,
    typename... RArgs,
    typename... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeRouteHandleWithInfo(
    Args&&... args) {
  return std::make_shared<typename RouterInfo::RouteHandleIf::template Impl<
      R<RouterInfo, RArgs...>>>(std::forward<Args>(args)...);
}

} // memcache
} // facebook
