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

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/MPMCQueue.h>

namespace facebook {
namespace memcache {

class CongestionControllerLogic {
 public:
  CongestionControllerLogic(
      uint64_t target,
      std::chrono::milliseconds delay,
      bool enableCPUControl,
      bool enableMemControl,
      size_t queueCapacity);

  ~CongestionControllerLogic();

  void updateValue(double value);

  double getDropProbability() const;

  void setTarget(uint64_t target);

 private:
  // The thread responsible for updating the probability.
  std::thread probabilityUpdateThread_;

  // The thread responsible for computing the weightedValue_.
  std::thread weightedValueUpdateThread_;

  // The thread responsible for logging the CPU utilization.
  std::thread cpuLoggingThread_;

  // The thread responsible for logging the memory utilization.
  std::thread memLoggingThread_;

  // Flag of keeping running the loops.
  std::atomic<bool> keepRunning_{true};

  /**
   * Flag indicating if in the first delay_ window. If in the first delay_
   * window, we apply the Simple Moving Average. For the following windows,
   * we leverage Exponential Moving Average.
   */
  std::atomic<bool> firstWindow_{true};

  // The target value to control. This value can be wait time, queue size, etc.
  std::atomic<uint64_t> target_{0};

  // The user provided update delay in milliseconds.
  std::chrono::milliseconds delay_{0};

  // Flag indicating if we are enabling CPU control or not.
  bool enableCPUControl_{false};

  // Flag indicating if we are enabling memory control or not.
  bool enableMemControl_{false};

  // Smoothing factor of the weighted moving average. The value is between 0
  // and 1. The closer to 1, the higher weight of the history data.
  std::atomic<double> smoothingFactor_{0.0};

  // Number of updates received in a RTT window.
  std::atomic<uint64_t> updateCounter_{0};

  // Send probability.
  std::atomic<double> sendProbability_{1.0};

  // Weighted value for the control.
  std::atomic<double> weightedValue_{0.0};

  // A queue for storing the values.
  folly::MPMCQueue<double> valueQueue_;
};

} // memcache
} // facebook
