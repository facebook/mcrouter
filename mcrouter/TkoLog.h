/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
 #pragma once

#include <string>

#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache { namespace mcrouter {

class AccessPoint;

enum class TkoLogEvent {
  MarkHardTko,
  MarkSoftTko,
  MarkLatencyTko,
  UnMarkTko
};

struct TkoLog {
  explicit TkoLog(const AccessPoint& ap);

  std::string eventName() const;

  TkoLogEvent event{TkoLogEvent::MarkHardTko};
  uintptr_t curSumFailures{0};
  size_t globalSoftTkos{0};
  bool isHardTko{false};
  bool isSoftTko{false};
  mc_res_t result;
  size_t probesSent{0};
  double avgLatency{0.0};
  const AccessPoint& accessPoint;
};

}}}  // facebook::memcache::mcrouter
