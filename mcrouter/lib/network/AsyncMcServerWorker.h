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

#include "mcrouter/lib/network/McServerRequestContext.h"

namespace folly {
class EventBase;
}

namespace apache { namespace thrift { namespace transport {
class SSLContext;
}}}

namespace facebook { namespace memcache {

class McServerOnRequest;
class McServerSession;

/**
 * A single threaded Memcache server.
 */
class AsyncMcServerWorker {
 public:

  /**
   * Worker options
   */
  struct Options {
    /**
     * String that will be returned for 'VERSION' commands.
     */
    std::string versionString{"AsyncMcServer-1.0"};

    /**
     * Timeout for writes (i.e. replies to the clients).
     * If 0, no timeout.
     */
    std::chrono::milliseconds sendTimeout{0};

    /**
     * Maximum number of unreplied requests allowed before
     * we stop reading from client sockets.
     * If 0, there is no limit.
     */
    size_t maxInFlight{0};

    /**
     * Maximum number of read system calls per event loop iteration.
     * If 0, there is no limit.
     *
     * If a socket has available data to read, we'll keep calling read()
     * on it this many times before we do any writes.
     *
     * For heavy workloads, larger values may hurt latency
     * but increase throughput.
     */
    uint16_t maxReadsPerEvent{0};

    /**
     * If non-zero, the buffer size will be dynamically adjusted
     * to contain roughly this many requests, within min/max limits below.
     *
     * The intention is to limit the number of requests processed
     * per loop iteration. Smaller values may improve latency.
     *
     * If 0, buffer size is always maxBufferSize.
     */
    size_t requestsPerRead{0};

    /**
     * Smallest allowed buffer size.
     */
    size_t minBufferSize{256};

    /**
     * Largest allowed buffer size.
     */
    size_t maxBufferSize{4096};

    /**
     * If true, we attempt to write every reply to the socket
     * immediately.  If the write cannot be fully completed (i.e. not
     * enough TCP memory), all reading is paused until after the write
     * is completed.
     */
    bool singleWrite{false};
  };

  /**
   * @param opts       Options
   * @param eventBase  eventBase that will process socket events
   */
  explicit AsyncMcServerWorker(Options opts,
                               folly::EventBase& eventBase);

  /**
   * Moves in ownership of an externally accepted client socket.
   */
  void addClientSocket(int fd);

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
   * Will be called once on closing every connection.
   * Note: this callback will be applied even to already open connections.
   * Can be nullptr for no action.
   */
  void setOnConnectionClosed(std::function<void()> cb) {
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
      apache::thrift::async::TAsyncSocket::UniquePtr&& socket);

  Options opts_;
  folly::EventBase& eventBase_;
  std::shared_ptr<McServerOnRequest> onRequest_;
  std::function<void()> onAccepted_;
  std::function<void()> onClosed_;
  std::function<void()> onShutdown_;

  bool isAlive_{true};

  /* Open sessions and closing sessions that still have pending writes */
  std::unordered_set<std::shared_ptr<McServerSession>> sessions_;

  AsyncMcServerWorker(const AsyncMcServerWorker&) = delete;
  AsyncMcServerWorker& operator=(const AsyncMcServerWorker&) = delete;

  AsyncMcServerWorker(AsyncMcServerWorker&&) noexcept = delete;
  AsyncMcServerWorker& operator=(AsyncMcServerWorker&&) = delete;
};


}}  // facebook::memcache
