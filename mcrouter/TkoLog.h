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

#include <folly/Range.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/TkoCounters.h"

namespace facebook { namespace memcache {

struct AccessPoint;

namespace mcrouter {

enum class TkoLogEvent {
  MarkHardTko,
  MarkSoftTko,
  RemoveFromConfig,
  UnMarkTko
};

struct TkoLog {
  TkoLog(const AccessPoint& ap, const TkoCounters& gt);

  std::string eventName() const;

  TkoLogEvent event{TkoLogEvent::MarkHardTko};
  uintptr_t curSumFailures{0};
  bool isHardTko{false};
  bool isSoftTko{false};
  mc_res_t result;
  size_t probesSent{0};
  double avgLatency{0.0};
  const AccessPoint& accessPoint;
  const TkoCounters& globalTkos;
  folly::StringPiece poolName;
};

}}}  // facebook::memcache::mcrouter
