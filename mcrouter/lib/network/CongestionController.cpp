/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CongestionController.h"

namespace facebook {
namespace memcache {

CongestionController::CongestionController(
    uint64_t target,
    std::chrono::milliseconds delay,
    bool enableCPUControl,
    size_t queueCapacity)
    : logic_(target, delay, enableCPUControl, queueCapacity) {}

void CongestionController::updateValue(double value) {
  logic_.updateValue(value);
}

double CongestionController::getDropProbability() const {
  return logic_.getDropProbability();
}

void CongestionController::setTarget(uint64_t target) {
  logic_.setTarget(target);
}

} // memcache
} // facebook
