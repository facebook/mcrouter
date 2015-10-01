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

#include <cmath>

namespace facebook { namespace memcache { namespace mcrouter {

template <size_t WindowSize>
class ExponentialSmoothData {
 public:
  static_assert(WindowSize > 0, "WindowSize should be > 0");

  void insertSample(double sample) {
    if (hasValue()) {
      currentValue_ = (sample + (WindowSize - 1) * currentValue_) / WindowSize;
    } else {
      currentValue_ = sample;
    }
  }

  double value() const {
    return hasValue() ? currentValue_ : 0.0;
  }

  bool hasValue() const {
    return !std::isnan(currentValue_);
  }

 private:
  double currentValue_{std::nan("")};
};

}}}  // facebook::memcache::mcrouter
