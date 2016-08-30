/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CongestionControllerLogic.h"

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

bool readProcStat(
    std::array<char, 320>& buf,
    std::array<uint64_t, 4>& curArray) {
  auto cpuStatFile = folly::File("/proc/stat");

  if (folly::readNoInt(cpuStatFile.fd(), buf.data(), buf.size()) !=
      buf.size()) {
    return false;
  }

  if (sscanf(
          buf.data(),
          "cpu %lu %lu %lu %lu",
          &curArray[0],
          &curArray[1],
          &curArray[2],
          &curArray[3]) != curArray.size()) {
    return false;
  }

  return true;
}

} // anonymous

CongestionControllerLogic::CongestionControllerLogic(
    uint64_t target,
    std::chrono::milliseconds delay,
    bool enableCPUControl,
    size_t queueCapacity)
    : target_(target),
      delay_(delay),
      enableCPUControl_(enableCPUControl),
      valueQueue_(queueCapacity) {
  // This thread updates the probability every delay_.
  probabilityUpdateThread_ = std::thread([this]() {
    while (keepRunning_) {
      // sleep override
      std::this_thread::sleep_for(delay_);

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
        continue;
      }

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
  });

  // This thread reads value from the valueQueue_, if any.
  weightedValueUpdateThread_ = std::thread([this]() {
    double curValue{0.0};

    while (keepRunning_) {
      if (valueQueue_.isEmpty()) {
        // sleep override
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      if (!valueQueue_.read(curValue)) {
        curValue = 0.0;
      }

      // Update the counter before updating weightedValue_.
      ++updateCounter_;

      if (firstWindow_) {
        weightedValue_ = weightedValue_ + curValue;
      } else {
        weightedValue_ = smoothingFactor_ * weightedValue_ +
            (1.0 - smoothingFactor_) * curValue;
      }
    }
  });

  if (!enableCPUControl_) {
    return;
  }

  // Compute the cpu utilization
  cpuLoggingThread_ = std::thread([this]() {
    std::array<uint64_t, 4> prev;
    std::array<uint64_t, 4> cur;
    double cpuUtil = 0.0;
    bool isFirst = true;

    // Enough storage for the /proc/stat CPU data needed below
    std::array<char, 320> buf;

    while (keepRunning_) {
      // sleep override
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Corner case: When parsing /proc/stat fails, set the cpuUtil to 0.
      if (!readProcStat(buf, cur)) {
        updateValue(0.0);
        continue;
      }

      if (isFirst) {
        isFirst = false;
      } else {
        /**
         * The values in the /proc/stat is the CPU time since boot.
         * 1st column is user, 2nd column is nice, 3rd column is system, and
         * the 4th column is idle. The total CPU time in the last window is
         * delta busy time over delta total time.
         */
        auto curUtil = cur[0] + cur[1] + cur[2];
        auto prevUtil = prev[0] + prev[1] + prev[2];
        auto utilDiff = static_cast<double>(curUtil - prevUtil);
        auto totalDiff = utilDiff + cur[3] - prev[3];

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

        updateValue(cpuUtil);
      }

      prev = cur;
    }
  });
}

CongestionControllerLogic::~CongestionControllerLogic() {
  keepRunning_ = false;

  probabilityUpdateThread_.join();
  weightedValueUpdateThread_.join();

  if (enableCPUControl_) {
    cpuLoggingThread_.join();
  }
}

void CongestionControllerLogic::updateValue(double value) {
  // Remove contention by using a queue here.
  valueQueue_.write(value);
}

double CongestionControllerLogic::getDropProbability() const {
  return 1.0 - sendProbability_;
}

void CongestionControllerLogic::setTarget(uint64_t target) {
  target_ = target;
}

} // memcache
} // facebook
