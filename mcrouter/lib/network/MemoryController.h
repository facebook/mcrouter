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

namespace facebook {
namespace memcache {

class MemoryController : public std::enable_shared_from_this<MemoryController> {
 public:
  MemoryController(
      const CongestionControllerOptions& opts,
      folly::EventBase& evb,
      size_t queueCapacity = 1000);

  double getDropProbability() const;

  void start();
  void stop();

 private:
  // The function responsible for logging the memory utilization.
  void memLoggingFn();

  folly::EventBase& evb_;
  bool firstLoop_{true};
  std::atomic<bool> stopController_{false};
  uint64_t target_;
  std::chrono::milliseconds dataCollectionInterval_;
  std::shared_ptr<CongestionController> logic_;
};

} // memcache
} // facebook
