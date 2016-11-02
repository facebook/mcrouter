/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CpuController.h"

#include <folly/File.h>
#include <folly/FileUtil.h>

namespace facebook {
namespace memcache {

namespace {

// The interval of reading /proc/stat and /proc/meminfo in milliseconds
constexpr double kReadProcInterval = 10;

bool readProcStat(std::vector<uint64_t>& curArray) {
  auto cpuStatFile = folly::File("/proc/stat");
  // Enough storage for the /proc/stat CPU data needed below
  std::array<char, 320> buf;
  if (folly::readNoInt(cpuStatFile.fd(), buf.data(), buf.size()) !=
      static_cast<ssize_t>(buf.size())) {
    return false;
  }

  if (sscanf(
          buf.data(),
          "cpu %lu %lu %lu %lu",
          &curArray[0],
          &curArray[1],
          &curArray[2],
          &curArray[3]) != static_cast<int>(curArray.size())) {
    return false;
  }

  return true;
}

} // anonymous

CpuController::CpuController(
    uint64_t target,
    folly::EventBase& evb,
    std::chrono::milliseconds delay,
    size_t queueCapacity)
    : evb_(evb),
      logic_(std::make_shared<CongestionController>(
          target,
          delay,
          evb,
          queueCapacity)) {}

double CpuController::getDropProbability() const {
  return logic_->getDropProbability();
}

void CpuController::start() {
  stopController_ = false;
  logic_->start();
  auto self = shared_from_this();
  evb_.runInEventBaseThread([this, self]() { cpuLoggingFn(); });
}

void CpuController::stop() {
  stopController_ = true;
  logic_->stop();
}

// Compute the cpu utilization
void CpuController::cpuLoggingFn() {
  double cpuUtil = 0.0;

  // Corner case: When parsing /proc/stat fails, set the cpuUtil to 0.
  std::vector<uint64_t> cur(4);
  if (readProcStat(cur)) {
    if (firstLoop_) {
      prev_ = std::move(cur);
      firstLoop_ = false;
    } else {
      /**
      * The values in the /proc/stat is the CPU time since boot.
      * 1st column is user, 2nd column is nice, 3rd column is system, and
      * the 4th column is idle. The total CPU time in the last window is
      * delta busy time over delta total time.
      */
      auto curUtil = cur[0] + cur[1] + cur[2];
      auto prevUtil = prev_[0] + prev_[1] + prev_[2];
      auto utilDiff = static_cast<double>(curUtil - prevUtil);
      auto totalDiff = utilDiff + cur[3] - prev_[3];

      /**
      * Corner case: If CPU didn't change or the proc/stat didn't get
      * updated or ticks didn't increase, set the cpuUtil to 0.
      */
      if (totalDiff < 0.001 || curUtil < prevUtil) {
        cpuUtil = 0.0;
      } else {
        // Corner case: The max of CPU utilization can be at most 100%.
        cpuUtil = std::min((utilDiff / totalDiff) * 100, 100.0);
      }
      prev_ = std::move(cur);
    }
  }
  if (stopController_) {
    logic_->updateValue(0.0);
    return;
  }
  logic_->updateValue(cpuUtil);
  auto self = shared_from_this();
  evb_.runAfterDelay([this, self]() { cpuLoggingFn(); }, kReadProcInterval);
}

} // memcache
} // facebook
