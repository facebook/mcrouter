/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <chrono>

namespace facebook {
namespace memcache {

/**
 * This controller is designed for a single thread and slow variable updates.
 * For multi-thread usage and frequent updated variables, please refer to
 * CongestionController.h.
 *
 * One use case of this controller is trying to control the queue size per
 * worker thread of AsyncMcServer.
 */
class CongestionControllerSingleThreaded {
 public:
  // Initialize the controller with a user-provided controlled target value and
  // delay. The drop probability will be updated every delay window.
  explicit CongestionControllerSingleThreaded(
      double target,
      std::chrono::milliseconds delay = std::chrono::milliseconds(100));

  CongestionControllerSingleThreaded(
      const CongestionControllerSingleThreaded&) = delete;
  CongestionControllerSingleThreaded& operator=(
      const CongestionControllerSingleThreaded&) = delete;

 // Update the value that needs to be controlled.
  void updateValue(double value);

  // Get the drop probability.
  double getDropProbability() const;

 private:
  double target_{0.0};

  std::chrono::milliseconds delay_{0};

  double weightedValue_{0.0};

  double sendProbability_{1.0};

  double smoothingFactor_{0.0};

  std::chrono::steady_clock::time_point startTime_;
  bool isStarted_{false};

  /**
   * Flag indicating if in the first delay_ window. If in the first delay_
   * window, we apply the Simple Moving Average. For the following windows,
   * we leverage Exponential Moving Average.
   */
  bool firstWindow_{true};

  // Counter of samples observed.
  uint32_t updateCounter_{0};

  double updateSmoothingFactor(std::chrono::milliseconds& timeDiff);
};

} // memcache
} // facebook
