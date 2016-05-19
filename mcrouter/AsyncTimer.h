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

#include <folly/io/async/AsyncTimeout.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

template <typename TOwner>
class AsyncTimer : public folly::AsyncTimeout {
 public:
  explicit AsyncTimer(TOwner& owner) : timerOwner_(owner) {}

  void timeoutExpired() noexcept final override {
    timerOwner_.timerCallback();
  }

 private:
  TOwner& timerOwner_;
};
} // mcrouter
} // memcache
} // facebook
