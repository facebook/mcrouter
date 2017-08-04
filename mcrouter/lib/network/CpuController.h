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

#include "mcrouter/lib/network/CongestionController.h"
#include "mcrouter/lib/network/ServerLoad.h"

namespace facebook {
namespace memcache {

class CpuController : public std::enable_shared_from_this<CpuController> {
 public:
  CpuController(
      const CongestionControllerOptions& opts,
      folly::EventBase& evb,
      size_t queueCapacity = 1000);

  double getDropProbability() const;

  /**
   * Gets the load on the server.
   */
  ServerLoad getServerLoad() const noexcept {
    return ServerLoad::fromPercentLoad(percentLoad_.load());
  }

  void start();
  void stop();

 private:
  // The function responsible for logging the CPU utilization.
  void cpuLoggingFn();

  // Updates cpu utilization value.
  void update(double cpuUtil);

  folly::EventBase& evb_;
  std::shared_ptr<CongestionController> logic_;
  std::vector<uint64_t> prev_{8};
  std::chrono::milliseconds dataCollectionInterval_;
  std::atomic<double> percentLoad_{0.0};
  std::atomic<bool> stopController_{false};
  bool enableDropProbability_{true};
  bool enableServerLoad_{true};
  bool firstLoop_{true};
};

} // memcache
} // facebook
