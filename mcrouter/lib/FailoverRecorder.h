/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/OperationTraits.h"

namespace facebook { namespace memcache {

class FailoverRecorder {
 public:
  FailoverRecorder()
      : size_(0),
        lastGetFailover_(std::vector<time_t>(0, 0)),
        lastUpdateFailover_(std::vector<time_t>(0, 0)),
        lastDeleteFailover_(std::vector<time_t>(0, 0)) {
  };

  explicit FailoverRecorder(size_t n)
      : size_(n),
        lastGetFailover_(std::vector<time_t>(n, 0)),
        lastUpdateFailover_(std::vector<time_t>(n, 0)),
        lastDeleteFailover_(std::vector<time_t>(n, 0)) {
  };
  FailoverRecorder(const FailoverRecorder&) = delete;
  FailoverRecorder(FailoverRecorder&&) = default;
  FailoverRecorder& operator=(const FailoverRecorder&) = delete;
  FailoverRecorder& operator=(FailoverRecorder&&) = delete;
  template <class Operation>
  void setLastFailover(size_t idx, Operation) {
    if (idx > size_ - 1) {
      return;
    }
    time_t now;
    time(&now);
    if (GetLike<Operation>::value) {
      lastGetFailover_[idx] = now;
    } else if (UpdateLike<Operation>::value) {
      lastUpdateFailover_[idx] = now;
    } else if (DeleteLike<Operation>::value) {
      lastDeleteFailover_[idx] = now;
    }
  }
  template <class Operation>
  time_t getLastFailover(size_t idx, Operation) const {
    if (idx > size_ - 1) {
      return 0;
    }
    if (GetLike<Operation>::value) {
      return lastGetFailover_[idx];
    } else if (UpdateLike<Operation>::value) {
      return lastUpdateFailover_[idx];
    } else if (DeleteLike<Operation>::value) {
      return lastDeleteFailover_[idx];
    }
    return 0;
  }
 private:
  const size_t size_;
  std::vector<time_t> lastGetFailover_;
  std::vector<time_t> lastUpdateFailover_;
  std::vector<time_t> lastDeleteFailover_;
};

}} // facebook::memcache
