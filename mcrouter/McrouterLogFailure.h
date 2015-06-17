/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/McrouterInstance.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <typename... Args>
void logFailure(folly::StringPiece category, folly::StringPiece msg,
                Args&&... args) {
  facebook::memcache::failure::log(
    "mcrouter", category, msg, std::forward<Args>(args)...);
}

template <typename... Args>
void logFailure(McrouterInstance& router,
                folly::StringPiece category,
                folly::StringPiece msg,
                Args&&... args) {
  facebook::memcache::failure::log(
    router.routerName(), category, msg, std::forward<Args>(args)...);
}

}}}  // facebook::memcache::mcrouter
