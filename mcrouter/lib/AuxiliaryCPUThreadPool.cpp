/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AuxiliaryCPUThreadPool.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

// Number of CPU threads to run in CPU thread pool.
constexpr size_t kNumCPUThreads = 5;

namespace {
folly::Singleton<AuxiliaryCPUThreadPool> gAuxiliaryCPUThreadPool;
} // anonymous

folly::CPUThreadPoolExecutor& AuxiliaryCPUThreadPool::getThreadPool() {
  folly::call_once(initFlag_, [&] {
    threadPool_ = std::make_unique<folly::CPUThreadPoolExecutor>(
        kNumCPUThreads,
        std::make_shared<folly::NamedThreadFactory>("mcr-cpuaux-"));
  });

  return *threadPool_;
}

} // mcrouter
} // memcache
} // facebook
