/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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

namespace facebook {
namespace memcache {

/**
 * Single threaded list of connections with LRU eviction logic.
 */
class ConnectionTracker : public McServerSession::StateCallback {
 public:
  /**
   * Creates a new tracker with `maxConns` connections. Once there are
   * more than `maxConns` connections (sessions), ConnectionTracker will close
   * the oldest one. If `maxConns` is 0, it will not close connections.
   */
  explicit ConnectionTracker(size_t maxConns = 0);

  // See AsyncMcServerWorker.h for details about the callbacks
  void setOnWriteQuiescence(std::function<void(McServerSession&)> cb) {
    onWriteQuiescence_ = std::move(cb);
  }

  void setOnConnectionCloseStart(std::function<void(McServerSession&)> cb) {
    onCloseStart_ = std::move(cb);
  }

  void setOnConnectionCloseFinish(std::function<void(McServerSession&)> cb) {
    onCloseFinish_ = std::move(cb);
  }

  void setOnShutdownOperation(std::function<void()> cb) {
    onShutdown_ = std::move(cb);
  }

  /**
   * Creates a new entry in the LRU and places the connection at the front.
   *
   * @return reference to the created session.
   * @throws std::runtime_exception when fails to create a session.
   */
  McServerSession& add(
      folly::AsyncTransportWrapper::UniquePtr transport,
      std::shared_ptr<McServerOnRequest> cb,
      const AsyncMcServerWorkerOptions& options,
      void* userCtxt,
      const CompressionCodecMap* compressionCodecMap);

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
  std::function<void(McServerSession&)> onWriteQuiescence_;
  std::function<void(McServerSession&)> onCloseStart_;
  std::function<void(McServerSession&)> onCloseFinish_;
  std::function<void()> onShutdown_;
  size_t maxConns_{0};

  void touch(McServerSession& session);

  void evict();

  // McServerSession::StateCallback API
  void onWriteQuiescence(McServerSession& session) final;
  void onCloseStart(McServerSession& session) final;
  void onCloseFinish(McServerSession& session) final;
  void onShutdown() final;
};
}
} // facebook::memcache
