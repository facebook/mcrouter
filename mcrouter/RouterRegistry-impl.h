/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdexcept>

#include <folly/Conv.h>

#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/network/gen/MemcacheServer.h"

/**
 * Finds the proper carbon api according to 'routerName', and calls 'func'
 * templated by the corresponding RouterInfo and RequestHandler.
 *
 * 'func' must be a function template, like the following:
 *
 * template <class RouterInfo, template <class> class RequestHandler>
 * void myFunc(...) {
 *   // ...
 * }
 *
 *
 * @param routerName  Name of the router (e.g. Memcache).
 * @param func        The function template that will be called.
 */
#define CALL_BY_ROUTER_NAME(routerName, func, ...)                        \
  do {                                                                    \
    if ((routerName) == facebook::memcache::MemcacheRouterInfo::name) {   \
      func<                                                               \
          facebook::memcache::MemcacheRouterInfo,                         \
          facebook::memcache::MemcacheRequestHandler>(__VA_ARGS__);       \
    } else {                                                              \
      throw std::invalid_argument(                                        \
          folly::to<std::string>("Invalid router name: ", (routerName))); \
    }                                                                     \
  } while (false);
