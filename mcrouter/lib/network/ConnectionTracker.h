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

#include <functional>
#include <memory>

#include "mcrouter/lib/network/McServerSession.h"

namespace facebook { namespace memcache {

/**
 * Single threaded list of connections with LRU eviction logic.
 */
class ConnectionTracker {
 public:
  /**
   * Creates a new tracker with `maxConns` connections. Once there are
   * more than `maxConns` connections (sessions), ConnectionTracker will close
   * the oldest one. If `maxConns` is 0, it will not close connections.
   */
  explicit ConnectionTracker(size_t maxConns = 0);

  /**
   * Creates a new entry in the LRU and places the connection at the front.
   *
   * @throws std::runtime_exception when fails to create a session.
   */
  void add(folly::AsyncTransportWrapper::UniquePtr transport,
           std::shared_ptr<McServerOnRequest> cb,
           std::function<void(McServerSession&)> onWriteQuiescence,
           std::function<void(McServerSession&)> onCloseStart,
           std::function<void(McServerSession&)> onCloseFinish,
           std::function<void()> onShutdown,
           AsyncMcServerWorkerOptions options,
           void* userCtxt,
           std::shared_ptr<Fifo> debugFifo);

  /**
   * Close all connections (sessions)
   */
  void closeAll();

  /**
   * Check if we have pending writes on any connection (session)
   */
  bool writesPending() const;
 private:
  McServerSession::Queue sessions_;
  size_t maxConns_{0};

  void touch(McServerSession& session);

  void evict();
};

}}  // facebook::memcache
