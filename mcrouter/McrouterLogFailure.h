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

#include <string>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook {
namespace memcache {

class McrouterOptions;

namespace mcrouter {

std::string routerName(const McrouterOptions& opts);

#define MC_LOG_FAILURE(opts, ...) LOG_FAILURE(routerName(opts), __VA_ARGS__)
}
}
} // facebook::memcache::mcrouter
