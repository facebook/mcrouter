/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ExponentialSmoothData.h"

#include <cassert>

namespace facebook { namespace memcache { namespace mcrouter {

ExponentialSmoothData::ExponentialSmoothData(double smoothingFactor)
  : smoothingFactor_(smoothingFactor) {
  assert(smoothingFactor_ >= 0.0 && smoothingFactor_ <= 1.0);
}

void ExponentialSmoothData::insertSample(double sample) {
  if (hasRegisteredFirstSample_) {
    currentValue_ = smoothingFactor_ * sample +
                    (1 - smoothingFactor_) * currentValue_;
  } else {
    currentValue_ = sample;
    hasRegisteredFirstSample_ = true;
  }
}

}}}  // facebook::memcache::mcrouter
