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

namespace facebook { namespace memcache { namespace mcrouter {

class ExponentialSmoothData {
 public:
  explicit ExponentialSmoothData(double smootingFactor);
  void insertSample(double sample);

  double value() const {
    return currentValue_;
  }

  bool hasValue() const {
    return hasRegisteredFirstSample_;
  }

 private:
  double smoothingFactor_{0.0};
  double currentValue_{0.0};
  bool hasRegisteredFirstSample_{false};
};

}}}  // facebook::memcache::mcrouter
