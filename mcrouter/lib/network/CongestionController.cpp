/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CongestionController.h"

#include <cassert>
#include <cmath>

namespace facebook {
namespace memcache {

namespace {

/**
 * Base probability: the lowest probability a client sends.
 * This value is initialized to 10^-1.
 */
constexpr double kBaseProbability = 0.1;

/**
 * Pecentage of the effect of a value after the delay_ window. Default to 0.1,
 * which is 10%. We keep the residue in the log form so that the residue can be
 * specified more intuitively. For example, if need 20% of residue, we only need
 * to update this value to log10(0.2).
 */
const double kLogResidue = log10(0.1);

} // anonymous

CongestionController::CongestionController(
    const CongestionControllerOptions& opts,
    folly::EventBase& evb,
    size_t queueCapacity)
    : target_(opts.target),
      delay_(opts.delay),
      evb_(evb),
      valueQueue_(queueCapacity) {
  assert(opts.shouldEnable());
}

void CongestionController::start() {
  stopController_ = false;
  auto self = shared_from_this();
  evb_.runInEventBaseThread([this, self]() { probabilityUpdateFn(); });
  evb_.runInEventBaseThread([this, self]() { weightedValueUpdateFn(); });
}

void CongestionController::stop() {
  stopController_ = true;
}

void CongestionController::probabilityUpdateFn() {
  // Get a copy of the current sendProbability_.
  double curSendProbability = sendProbability_;
  // Get a copy the counter value.
  uint32_t curUpdateCounter = updateCounter_;
  // Get a copy of the current weightedValue_.
  double curWeightedValue = weightedValue_;

  /**
   * Corner case: If nothing is received this window, reset the
   * sendProbability_ to 1.
   */
  if (curUpdateCounter == 0) {
    sendProbability_ = 1.0;
  } else {
    if (firstWindow_) {
      curWeightedValue /= curUpdateCounter;
      firstWindow_ = false;
    }

    /**
     * Current adjustment factor. Default set sendProbability = 1.0 if
     * curWeightedValue is close to 0.
     */
    if (curWeightedValue > 0.000001) {
      // Update the send probability.
      curSendProbability *= target_ / curWeightedValue;
    } else {
      curSendProbability = 1.0;
    }

    if (curSendProbability < kBaseProbability) {
      curSendProbability = kBaseProbability;
    } else if (curSendProbability > 1.0) {
      curSendProbability = 1.0;
    }

    // Update the atomic probability here.
    sendProbability_ = curSendProbability;

    // Reset the counter.
    updateCounter_ -= curUpdateCounter;

    /* Update the smoothingFactor_.
     *
     * We want the effect of current update fades after delay_, so
     * we keep the curUpdateCounter and increase it on every updateValue call.
     *
     * Suppose the target smoothingFactor_ is x, then we want
     *
     *    x^curUpdateCounter < fadeThreshold
     *
     * which we set the fadeThreshold to 10%
     *
     *    x^curUpdateCounter = 10^-1
     *
     * then x = 10^(-1 / curUpdateCounter)
     */
    smoothingFactor_ =
        pow(10, kLogResidue / static_cast<double>(curUpdateCounter));
  }
  if (stopController_) {
    // reset to default value to stop in clean state
    sendProbability_ = 1.0;
    return;
  }
  auto self = shared_from_this();
  evb_.runAfterDelay([this, self]() { probabilityUpdateFn(); }, delay_.count());
}

void CongestionController::weightedValueUpdateFn() {
  double curValue{0.0};
  uint64_t delayMs = 0;

  if (valueQueue_.readIfNotEmpty(curValue)) {
    // Update the counter before updating weightedValue_.
    ++updateCounter_;

    if (firstWindow_) {
      weightedValue_ = weightedValue_ + curValue;
    } else {
      weightedValue_ = smoothingFactor_ * weightedValue_ +
          (1.0 - smoothingFactor_) * curValue;
    }
  } else {
    // if the queue is empty, attempt reading again after 1ms
    delayMs = 1;
  }

  if (stopController_) {
    return;
  }
  auto self = shared_from_this();
  evb_.runAfterDelay([this, self]() { weightedValueUpdateFn(); }, delayMs);
}

void CongestionController::updateValue(double value) {
  // Remove contention by using a queue here.
  valueQueue_.write(value);
}

double CongestionController::getDropProbability() const {
  return 1.0 - sendProbability_;
}

void CongestionController::setTarget(uint64_t target) {
  target_ = target;
}

} // memcache
} // facebook
