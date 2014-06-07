/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <condition_variable>
#include <mutex>

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;

class ProxyThread {
 public:
  proxy_t* proxy;
  explicit ProxyThread(proxy_t* proxy_);

  /**
   * Stops the underlyting proxy thread and joins it.
   */
  void stopAndJoin();

  /**
   * Spawns a new proxy thread for execution.
   */
  int spawn();

 private:
  pthread_t thread_handle;
  void *thread_stack;
  std::mutex mux;
  std::condition_variable cv;
  bool isSafeToDeleteProxy;

  void stopAwriterThreads();
  void proxyThreadRun();
  static void *proxyThreadRunHandler(void *arg);
};


}}}  // facebook::memcache::mcrouter
