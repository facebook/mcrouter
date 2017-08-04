/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>

#include <folly/MPMCQueue.h>
#include <folly/io/async/EventBase.h>

namespace folly {
class EventBase;
}

namespace facebook {
namespace memcache {

struct CongestionControllerOptions {
  /**
   * How frequently we should collect data.
   * 0 to disable collecting data completely.
   */
  std::chrono::milliseconds dataCollectionInterval{0};

  /**
   * Whether to enable sending server load back to clients.
   */
  bool enableServerLoad{false};

  /**
   * The target value to the controller. This value is used to calculate
   * drop probability.
   * 0 to disable drop probability calculation.
   */
  uint64_t target{0};

  /**
   * The update delay of drop probability.
   */
  std::chrono::milliseconds delay{100};

  bool shouldEnable() const noexcept {
    return dataCollectionInterval.count() > 0 &&
        (enableServerLoad || (target > 0 && delay >= dataCollectionInterval));
  }
};

/**
 * This class provides simple APIs to control a variable with the user-provided
 * target value. The use case of this controller is to throttle clients if
 * server if overloaded. That is, if the variable is more than the target, the
 * server will calculate a drop probability so that the clients will drop
 * requests given this probability.
 */

class CongestionController
    : public std::enable_shared_from_this<CongestionController> {
 public:
  CongestionController(
      const CongestionControllerOptions& opts,
      folly::EventBase& evb,
      size_t queueCapacity);

  CongestionController(const CongestionController&) = delete;
  CongestionController& operator=(const CongestionController&) = delete;

  // Update the value that needs to be controlled.
  void updateValue(double value);

  // Get the drop probability.
  double getDropProbability() const;

  // Reset the target.
  void setTarget(uint64_t target);

  void start();
  void stop();

 private:
  // The function responsible for updating the probability.
  void probabilityUpdateFn();

  // The function responsible for computing the weightedValue_.
  void weightedValueUpdateFn();

  /**
   * Flag indicating if in the first delay_ window. If in the first delay_
   * window, we apply the Simple Moving Average. For the following windows,
   * we leverage Exponential Moving Average.
   */
  bool firstWindow_{true};
  std::atomic<bool> stopController_{false};

  // The target value to control. This value can be wait time, queue size, etc.
  std::atomic<uint64_t> target_{0};

  // The user provided update delay in milliseconds.
  std::chrono::milliseconds delay_{0};

  folly::EventBase& evb_;

  // Smoothing factor of the weighted moving average. The value is between 0
  // and 1. The closer to 1, the higher weight of the history data.
  double smoothingFactor_{0.0};

  // Number of updates received in a RTT window.
  uint64_t updateCounter_{0};

  // Send probability.
  std::atomic<double> sendProbability_{1.0};

  // Weighted value for the control.
  double weightedValue_{0.0};

  // A queue for storing the values.
  folly::MPMCQueue<double> valueQueue_;
};

} // memcache
} // facebook
