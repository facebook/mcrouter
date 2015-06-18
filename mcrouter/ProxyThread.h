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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <folly/io/async/EventBase.h>

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterInstance;
class proxy_t;

class ProxyThread {
 public:
  explicit ProxyThread(McrouterInstance& router);

  /**
   * Stops the underlyting proxy thread and joins it.
   */
  void stopAndJoin();

  /**
   * Spawns a new proxy thread for execution.
   *
   * @throws std::system_error  If failed to spawn thread
   */
  void spawn();

  proxy_t& proxy() { return *proxy_; }
  folly::EventBase& eventBase() { return evb_; }

 private:
  folly::EventBase evb_;
  std::unique_ptr<proxy_t> proxy_;
  std::thread thread_;
  std::mutex mux;
  std::condition_variable cv;
  bool isSafeToDeleteProxy{false};

  void stopAwriterThreads();
  void proxyThreadRun();
  static void *proxyThreadRunHandler(void *arg);
};


}}}  // facebook::memcache::mcrouter
