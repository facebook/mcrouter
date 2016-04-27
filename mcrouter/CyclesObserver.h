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

#include <folly/experimental/ExecutionObserver.h>

namespace facebook { namespace memcache { namespace mcrouter {

class CyclesObserver : public folly::ExecutionObserver {
  void starting(uintptr_t id) noexcept override final;
  void runnable(uintptr_t id) noexcept override final;
  void stopped(uintptr_t id) noexcept override final;
};

}}} // facebook::memcache::mcrouter
