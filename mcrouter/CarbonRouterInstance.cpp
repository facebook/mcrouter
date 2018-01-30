/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CarbonRouterInstance.h"

#include <folly/io/async/EventBase.h>
#include <folly/json.h>

#include "mcrouter/lib/AuxiliaryCPUThreadPool.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

McrouterManager::McrouterManager() {
  scheduleSingletonCleanup();
  // Instantiate AuxiliaryCPUThreadPoolSingleton to make sure that it gets
  // destroyed after McrouterManager is destroyed.
  AuxiliaryCPUThreadPoolSingleton::try_get();
}

McrouterManager::~McrouterManager() {
  freeAllMcrouters();
}

void McrouterManager::freeAllMcrouters() {
  std::lock_guard<std::mutex> lg(mutex_);
  mcrouters_.clear();
}

bool isValidRouterName(folly::StringPiece name) {
  if (name.empty()) {
    return false;
  }

  for (auto c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || (c == '_') || (c == '-'))) {
      return false;
    }
  }

  return true;
}

folly::Singleton<McrouterManager> gMcrouterManager;

} // detail

void freeAllRouters() {
  if (auto manager = detail::gMcrouterManager.try_get()) {
    manager->freeAllMcrouters();
  }
}

} // mcrouter
} // memcache
} // facebook
