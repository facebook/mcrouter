/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Singleton.h>
#include <folly/synchronization/CallOnce.h>
#include <wangle/concurrent/CPUThreadPoolExecutor.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * CPU Thread pool that is shared between router intances.
 *
 * Should not be used directly, use AuxiliaryCPUThreadPoolSingleton instead.
 * Thread pool is lazily initialized on first call to getThreadPool().
 */
class AuxiliaryCPUThreadPool {
 public:
  wangle::CPUThreadPoolExecutor& getThreadPool();

 private:
  std::unique_ptr<wangle::CPUThreadPoolExecutor> threadPool_;
  folly::once_flag initFlag_;
};

using AuxiliaryCPUThreadPoolSingleton =
    folly::Singleton<AuxiliaryCPUThreadPool>;

} // mcrouter
} // memcache
} // facebook
