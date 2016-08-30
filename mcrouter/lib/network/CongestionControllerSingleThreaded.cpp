/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CongestionControllerSingleThreaded.h"

#include <cmath>

namespace facebook {
namespace memcache {

// The base send probability: the send probability must be less than this value
// at any time.
const double kBaseProbability{0.1};

/**
 * Pecentage of the effect of a value after the delay_ window. Default to 0.1,
 * which is 10%. We keep the residue in the log form so that the residue can be
 * specified more intuitively. For example, if need 20% of residue, we only need
 * to update this value to log10(0.2).
 */
const double kLogResidue = log10(0.1);

CongestionControllerSingleThreaded::CongestionControllerSingleThreaded(
    double target,
    std::chrono::milliseconds delay)
    : target_(target), delay_(delay) {}

void CongestionControllerSingleThreaded::updateValue(double value) {
  ++updateCounter_;

  // Assign the value to weightedValue_ for initialization.
  if (!isStarted_) {
    startTime_ = std::chrono::steady_clock::now();
    isStarted_ = true;

    weightedValue_ = value;

    return;
  }

  // Get current time diff.
  auto curTime = std::chrono::steady_clock::now();

  // Compute the current time diff.
  auto curTimeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
      curTime - startTime_);

  /**
   * If we are still in the first window, we accumulate the values for the
   * computation of average later. Otherwise, we update the weightedValue_ using
   * exponential moving average.
   */
  if (firstWindow_) {
    weightedValue_ += value;
  } else {
    weightedValue_ =
        smoothingFactor_ * weightedValue_ + (1.0 - smoothingFactor_) * value;
  }

  if (curTimeDiff > delay_) {
    // If this is the first window, the weightedValue_ is the average.
    if (firstWindow_) {
      weightedValue_ = weightedValue_ / updateCounter_;
      firstWindow_ = false;
    }

    // Update the smoothingFactor_.
    smoothingFactor_ = updateSmoothingFactor(curTimeDiff);

    // Default set sendProbability = 1.0 if curWeightedValue is close to 0.
    if (weightedValue_ > 0.000001) {
      // Update sendProbability_.
      sendProbability_ *= target_ / weightedValue_;
    } else {
      sendProbability_ = 1.0;
    }

    if (sendProbability_ < kBaseProbability) {
      sendProbability_ = kBaseProbability;
    } else if (sendProbability_ > 1.0) {
      sendProbability_ = 1.0;
    }

    // Update the startTime_.
    startTime_ = curTime;

    // Reset the updateCounter_.
    updateCounter_ = 0;
  }
}

double CongestionControllerSingleThreaded::getDropProbability() const {
  return 1.0 - sendProbability_;
}

/* Update the smoothingFactor_ so that the effect of the current value
 * has <= RESIDUE after delay_ window. We calcualte the actual time elapsed to
 * compute the normalized updateCounter_ per delay_ window.
 *
 * Suppose the target smoothingFactor_ is x, then we want
 *
 *    x^normalizedUpdateCounter < fadeThreashold
 *
 * If we set the fadeThreashold to 10%
 *
 *    x^normalizedUpdateCounter = 10^-1
 *
 * then x = 10^(-1 / normalizedUpdateCounter)
 */
double CongestionControllerSingleThreaded::updateSmoothingFactor(
    std::chrono::milliseconds& timeDiff) {
  return pow(
      10,
      kLogResidue / (static_cast<double>(updateCounter_) * delay_.count() /
                     timeDiff.count()));
}

} // memcache
} // facebook
