/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TkoLog.h"

#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook { namespace memcache { namespace mcrouter {

TkoLog::TkoLog(const AccessPoint& ap, const TkoCounters& gt)
  : accessPoint(ap),
    globalTkos(gt) {
}

std::string TkoLog::eventName() const {
  switch (event) {
    case TkoLogEvent::MarkHardTko:
      return "mark_hard_tko";
    case TkoLogEvent::MarkSoftTko:
      return "mark_soft_tko";
    case TkoLogEvent::RemoveFromConfig:
      return "remove_from_config";
    case TkoLogEvent::UnMarkTko:
      return "unmark_tko";
    default:
      return "unknown";
  }
}

}}}  // facebook::memcache::mcrouter
