/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <chrono>
#include <memory>
#include <unordered_set>

#include <thrift/lib/cpp/async/TAsyncSocket.h>

#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/McServerSession.h"

namespace folly {
class EventBase;
}

namespace apache { namespace thrift { namespace transport {
class SSLContext;
}}}

namespace facebook { namespace memcache {

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
      const std::shared_ptr<apache::thrift::transport::SSLContext>& context);

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
   * Will be called when reply is successfuly written out to the transport.
   * The callback is passed the McServerSession object that wrote the reply.
   *
   * Note: This doesn't apply to already open connections
   */
  void setOnWriteSuccess(std::function<void(McServerSession&)> cb) {
    onWriteSuccess_ = std::move(cb);
  }

  /**
   * Will be called once on closing every connection. The McServerSession
   * that was closed is passed in as a parameter.
   *
   * Note: this callback will be applied even to already open connections.
   * Can be nullptr for no action.
   */
  void setOnConnectionClosed(std::function<void(McServerSession&)> cb) {
    onClosed_ = std::move(cb);
  }

  /**
   * Will be called when a 'SHUTDOWN' operation is processed.
   */
  void setOnShutdownOperation(std::function<void()> cb) {
    onShutdown_ = std::move(cb);
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
      apache::thrift::async::TAsyncSocket::UniquePtr&& socket,
      void* userCtxt);

  AsyncMcServerWorkerOptions opts_;
  folly::EventBase& eventBase_;
  std::shared_ptr<McServerOnRequest> onRequest_;
  std::function<void()> onAccepted_;
  std::function<void(McServerSession&)> onWriteSuccess_;
  std::function<void(McServerSession&)> onClosed_;
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
