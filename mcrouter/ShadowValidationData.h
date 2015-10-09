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

#include <string>
#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache {

struct AccessPoint;

namespace mcrouter {

struct ShadowValidationData {
  const char* const operationName;
  const AccessPoint* normalDest;
  const AccessPoint* shadowDest;
  uint64_t normalFlags;
  uint64_t shadowFlags;
  mc_res_t normalResult;
  mc_res_t shadowResult;
  folly::StringPiece fullKey;
};

}}}  // facebook::memcache::mcrouter
