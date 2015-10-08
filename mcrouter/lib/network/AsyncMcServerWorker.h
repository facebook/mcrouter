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

#include <chrono>
#include <memory>
#include <unordered_set>

#include <folly/io/async/AsyncSocket.h>
#include <folly/Optional.h>

#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/ConnectionLRU.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/McServerSession.h"

namespace folly {
class EventBase;
class SSLContext;
}

namespace facebook { namespace memcache {

class Fifo;
class McServerOnRequest;

/**
 * A single threaded Memcache server.
 */
class AsyncMcServerWorker {
 public:
  /**
   * @param opts       Options
   * @param eventBase  eventBase that will process socket events
   */
  explicit AsyncMcServerWorker(AsyncMcServerWorkerOptions opts,
                               folly::EventBase& eventBase);

  /**
   * Moves in ownership of an externally accepted client socket.
   */
  void addClientSocket(int fd, void* userCtxt = nullptr);

  /**
   * Moves in ownership of an externally accepted client socket with an ssl
   * context which will be used to manage it.
   */
  void addSecureClientSocket(
      int fd,
      const std::shared_ptr<folly::SSLContext>& context,
      void* userCtxt = nullptr);

  /**
   * Install onRequest callback to call for all new connections.
   *
   * The callback must be a class with public methods specializing the template
   * onRequest:
   *
   * struct OnRequest {
   *   // ctx and req are guaranteed to be alive
   *   // until ctx.sendReply() is called, even after this method exits
   *   template <class Request, class Operation>
   *   void onRequest(McServerRequestContext& ctx,
   *                  const Request& req, Operation);
   * };
   *
   * Each onRequest call must eventually result in a call to ctx.sendReply().
   *
   * @param callback  New callback.  Note: a callback must be
   *                  installed before calling addClientSocket().
   */
  template <class OnRequest>
  void setOnRequest(OnRequest onRequest) {
    static_assert(std::is_class<OnRequest>::value,
                  "setOnRequest(): onRequest must be a class type");
    onRequest_ = std::make_shared<McServerOnRequestWrapper<OnRequest>>(
      std::move(onRequest));
  }

  /**
   * Will be called once for every new accepted connection.
   * Can be nullptr for no action.
   */
  void setOnConnectionAccepted(std::function<void()> cb) {
    onAccepted_ = std::move(cb);
  }

  /**
   * Will be called when all pending replies are successfuly written out to the
   * transport. The callback is passed the McServerSession object that wrote the
   * reply as the argument.
   *
   * Note: This doesn't apply to already open connections
   */
  void setOnWriteQuiescence(std::function<void(McServerSession&)> cb) {
    onWriteQuiescence_ = std::move(cb);
  }

  /**
   * Will be called once on every connection on which close was inititated.
   * This callback allows applications to initiate any cleanup process, before
   * the connection is actually closed (see onCloseFinish).
   *
   * The associated McServerSession is passed as the argument to the
   * callback.
   *
   * Note: this callback will be applied even to already open connections.
   * Can be nullptr for no action.
   */
  void setOnConnectionCloseStart(std::function<void(McServerSession&)> cb) {
    onCloseStart_ = std::move(cb);
  }

  /**
   * Will be called once on closing every connection. The McServerSession
   * that was closed is passed in as a parameter. This callback marks the
   * end of the session lifetime and the session is no longer valid after
   * the callback.
   *
   * Note: this callback will be applied even to already open connections.
   * Can be nullptr for no action.
   */
  void setOnConnectionCloseFinish(std::function<void(McServerSession&)> cb) {
    onCloseFinish_ = std::move(cb);
  }

  /**
   * Will be called when a 'SHUTDOWN' operation is processed.
   */
  void setOnShutdownOperation(std::function<void()> cb) {
    onShutdown_ = std::move(cb);
  }

  void setDebugFifo(Fifo* debugFifo) {
    debugFifo_ = debugFifo;
  }

  /**
   * Start closing all connections.
   * All incoming requests must still be replied by the application,
   * and all pending writes must drain before worker can be destroyed cleanly.
   */
  void shutdown();

  /**
   * Returns false iff shutdown() had already been called.
   */
  bool isAlive() const {
    return isAlive_;
  }

  /**
   * If true, there are some unfinished writes.
   */
  bool writesPending() const;

 private:
  void addClientSocket(
      folly::AsyncSocket::UniquePtr&& socket,
      void* userCtxt);

  AsyncMcServerWorkerOptions opts_;
  folly::EventBase& eventBase_;
  Fifo* debugFifo_{nullptr};

  struct McServerSessionDeleter {
    void operator() (McServerSession* session) const {
      session->close();
    }
  };

  folly::Optional<ConnectionLRU<std::unique_ptr<McServerSession,
                                McServerSessionDeleter>>> connLRU_{folly::none};
  std::shared_ptr<McServerOnRequest> onRequest_;
  std::function<void()> onAccepted_;
  std::function<void(McServerSession&)> onWriteQuiescence_;
  std::function<void(McServerSession&)> onCloseStart_;
  std::function<void(McServerSession&)> onCloseFinish_;
  std::function<void()> onShutdown_;

  bool isAlive_{true};

  /* Open sessions and closing sessions that still have pending writes */
  McServerSession::Queue sessions_;

  AsyncMcServerWorker(const AsyncMcServerWorker&) = delete;
  AsyncMcServerWorker& operator=(const AsyncMcServerWorker&) = delete;

  AsyncMcServerWorker(AsyncMcServerWorker&&) noexcept = delete;
  AsyncMcServerWorker& operator=(AsyncMcServerWorker&&) = delete;
};


}}  // facebook::memcache
