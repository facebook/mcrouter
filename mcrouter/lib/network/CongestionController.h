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

#include "CongestionControllerLogic.h"

namespace facebook {
namespace memcache {

struct CongestionControllerOptions {
  // The target value to the controller of CPU utilization
  uint64_t cpuControlTarget{0};

  // Worker-specific options
  std::chrono::milliseconds cpuControlDelay{0};
};

// Stats of the controller
struct CongestionControllerStats {
  double dropProbability{0.0};
};

/**
 * This class provides simple APIs to control a variable with the user-provided
 * target value. The use case of this controller is to throttle clients if
 * server if overloaded. That is, if the variable is more than the target, the
 * server will calculate a drop probability so that the clients will drop
 * requests given this probability.
 *
 * There is CPU utilization controller implemented internally and can be enabled
 * in the constructor.
 */
class CongestionController {
 public:
  /**
   * Initialize the CongestionController with a user-provided target. By
   * default, we set the delay to 100 milliseconds, disable the CPU controller,
   * and the queue to store update values to 1000.
   */
  explicit CongestionController(
      uint64_t target,
      std::chrono::milliseconds delay = std::chrono::milliseconds(100),
      bool enableCPUControl = false,
      size_t queueCapacity = 1000);

  CongestionController(const CongestionController&) = delete;
  CongestionController& operator=(const CongestionController&) = delete;

  // Update the value that needs to be controlled.
  void updateValue(double value);

  // Get the drop probability.
  double getDropProbability() const;

  // Set the target.
  void setTarget(uint64_t target);

  CongestionControllerStats getStats() const;

 private:
  CongestionControllerLogic logic_;
};

} // memcache
} // facebook
