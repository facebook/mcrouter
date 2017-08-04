/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MemoryController.h"

#include <cassert>

#include <folly/File.h>
#include <folly/FileUtil.h>

namespace facebook {
namespace memcache {

namespace {

bool readMemInfo(std::vector<uint64_t>& curArray) {
  auto memStatFile = folly::File("/proc/meminfo");
  // Enough storage for the /proc/meminfo memory info needed below
  std::array<char, 320> buf;
  if (folly::readNoInt(memStatFile.fd(), buf.data(), buf.size()) !=
      static_cast<ssize_t>(buf.size())) {
    return false;
  }

  if (sscanf(
          buf.data(),
          "MemTotal:%*[ \t]%lu%*s\nMemFree:%*[ \t]%lu%*s\n \
          MemAvailable:%*[ \t]%lu%*s\n",
          &curArray[0],
          &curArray[1],
          &curArray[2]) != static_cast<int>(curArray.size())) {
    return false;
  }

  return true;
}

} // anonymous

MemoryController::MemoryController(
    const CongestionControllerOptions& opts,
    folly::EventBase& evb,
    size_t queueCapacity)
    : evb_(evb),
      target_(opts.target),
      dataCollectionInterval_(opts.dataCollectionInterval),
      logic_(std::make_shared<CongestionController>(opts, evb, queueCapacity)) {
  assert(opts.shouldEnable());
}

double MemoryController::getDropProbability() const {
  return logic_->getDropProbability();
}

void MemoryController::start() {
  stopController_ = false;
  auto self = shared_from_this();
  evb_.runInEventBaseThread([this, self]() { memLoggingFn(); });
  logic_->start();
}

void MemoryController::stop() {
  stopController_ = true;
  logic_->stop();
}

void MemoryController::memLoggingFn() {
  double memUtil = 0.0;
  double totalMem = 0.0;

  std::vector<uint64_t> cur(3);
  if (readMemInfo(cur)) {
    // Here we convert the target_ in KB to the percentage.
    totalMem = static_cast<double>(cur[0]);
    if (totalMem != 0.0 && firstLoop_) {
      // The precision of the memory usage is 10^-6.
      logic_->setTarget(((totalMem - target_) / totalMem) * 1000000.0);
      firstLoop_ = false;
    }

    auto availMem = static_cast<double>(cur[2]);

    if (totalMem == 0.0 || availMem > totalMem) {
      memUtil = 0.0;
    } else {
      memUtil = ((totalMem - availMem) / totalMem) * 1000000.0;
    }
  }
  if (stopController_) {
    logic_->updateValue(0.0);
    return;
  }
  logic_->updateValue(memUtil);
  auto self = shared_from_this();
  evb_.runAfterDelay(
      [this, self]() { memLoggingFn(); }, dataCollectionInterval_.count());
}

} // memcache
} // facebook
