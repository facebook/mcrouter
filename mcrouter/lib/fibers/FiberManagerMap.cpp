/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/fibers/FiberManagerMap.h"

#include <memory>
#include <unordered_map>

namespace facebook { namespace memcache {

namespace detail {

thread_local std::unordered_map<folly::EventBase*, FiberManager*>
    localFiberManagerMap;
std::unordered_map<folly::EventBase*, std::unique_ptr<FiberManager>>
    fiberManagerMap;
std::mutex fiberManagerMapMutex;

FiberManager* getFiberManagerThreadSafe(folly::EventBase& evb,
                                        const FiberManager::Options& opts) {
  using namespace mcrouter;
  std::lock_guard<std::mutex> lg(fiberManagerMapMutex);

  auto it = fiberManagerMap.find(&evb);
  if (LIKELY(it != fiberManagerMap.end())) {
    return it->second.get();
  }

  auto loopController = folly::make_unique<EventBaseLoopController>();
  loopController->attachEventBase(evb);
  auto fiberManager =
      folly::make_unique<FiberManager>(std::move(loopController), opts);
  auto result = fiberManagerMap.emplace(&evb, std::move(fiberManager));
  return result.first->second.get();
}

} // detail namespace

FiberManager& getFiberManager(folly::EventBase& evb,
                              const FiberManager::Options& opts) {
  auto it = detail::localFiberManagerMap.find(&evb);
  if (LIKELY(it != detail::localFiberManagerMap.end())) {
    return *(it->second);
  }

  return *(detail::localFiberManagerMap[&evb] =
               detail::getFiberManagerThreadSafe(evb, opts));
}

}}
