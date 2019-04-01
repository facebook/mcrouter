/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/network/AsyncMcClientImpl.h"
#include "mcrouter/lib/network/ConnectionDownReason.h"
#include "mcrouter/lib/network/ConnectionOptions.h"

namespace folly {
class AsyncSocket;
class EventBase;
} // folly

namespace facebook {
namespace memcache {

struct RpcStatsContext;

/**
 * A class for network communication with memcache protocol.
 *
 * This class serves as a public interface and gateway to the client
 * implementation. It guarantees that all requests will be processed even after
 * this client was destroyed (i.e. the base client will be kept alive as long
 * as we have at least one request, but it will be impossible to send more
 * requests).
 */
class AsyncMcClient {
 public:
  using FlushList = AsyncMcClientImpl::FlushList;

  AsyncMcClient(folly::EventBase& eventBase, ConnectionOptions options);
  AsyncMcClient(folly::VirtualEventBase& eventBase, ConnectionOptions options);
  ~AsyncMcClient() {
    base_->setFlushList(nullptr);
  }

  /**
   * Close connection and fail all outstanding requests immediately.
   */
  void closeNow();

  /**
   * Set status callbacks for the underlying connection.
   *
   * @param onUp  Will be called whenever client successfully connects to the
   *              server. Will be called immediately if we're already connected.
   *              Can be nullptr.
   * @param onDown  will be called whenever connection goes down. Will be passed
   *                explanation about why the connection went down.
   *                Will not be called if the connection is already DOWN.
   *                Can be nullptr.
   * Note: those callbacks may be called even after the client was destroyed.
   *       This will happen in case when the client is destroyed and there are
   *       some requests left, for wich reply callback wasn't called yet.
   */
  void setStatusCallbacks(
      std::function<void(
          const folly::AsyncTransportWrapper& socket,
          int64_t numConnectRetries)> onUp,
      std::function<void(ConnectionDownReason, int64_t numConnectRetires)>
          onDown);

  /**
   * Set callbacks for when requests state change.
   *
   * @param onStateChange   Will be called whenever a request changes state.
   *                        pendingDiff and inflightDiff will hold the
   *                        difference in the number of pending and inflight
   *                        requests, respectively.
   * @param onWrite         Will be called everytime AsyncMcClient is about to
   *                        write data to network. The numToSend argument
   *                        holds the number of requests that will be sent in
   *                        a single batch.
   * @param onPartialWrite  Will be called everytime a partial write happened.
   *                        This means we would block when performing the full
   *                        write, so we buffered some data to try again when
   *                        the transport becomes ready to be written to again.
   */
  void setRequestStatusCallbacks(
      std::function<void(int pendingDiff, int inflightDiff)> onStateChange,
      std::function<void(size_t numToSend)> onWrite,
      std::function<void()> onPartialWrite);

  /**
   * Send request synchronously (i.e. blocking call).
   * NOTE: it must be called only from fiber context. It will block the current
   *       stack and will send request only when we loop EventBase.
   *
   * @param request       The request to send.
   * @param timeout       The timeout of this call.
   * @param rpcContext    Output argument that can be used to return information
   *                      about the reply received. If nullptr, it will be
   *                      ignored (i.e. no information is going be sent back up)
   */
  template <class Request>
  ReplyT<Request> sendSync(
      const Request& request,
      std::chrono::milliseconds timeout,
      RpcStatsContext* rpcContext = nullptr);

  /**
   * Set throttling options.
   *
   * @param maxInflight  max number of requests that can be waiting for the
   *                     network reply (0 means unlimited).
   * @param maxPending  max number of requests that can be waiting to be
   *                    sent over the network (0 means unlimited). If on attempt
   *                    to send a new request we're going to exceed this limit,
   *                    then that request would fail and the callback would be
   *                    called with a proper error code immediately
   *                    (e.g. local error).
   *                    Also user should not expect to be able to put more
   *                    than maxPending requests into the queue at once (i.e.
   *                    user should not expect to be able to send
   *                    maxPending+maxInflight requests at once).
   * Note: will not affect already sent or pending requests. None of them would
   *       be dropped.
   */
  void setThrottle(size_t maxInflight, size_t maxPending);

  /**
   * Get the number of requests in pending queue. Those requests have not been
   * sent to the network yet, this means that in case of remote error we can
   * still try to send them.
   */
  size_t getPendingRequestCount() const;

  /**
   * Get the number of requests in inflight queue. This amounts for requests
   * that are currently been written to the socket and requests that were
   * already sent to the server and are waiting for replies. Those requests
   * might be already processed by the server, thus they wouldn't be
   * retransmitted in case of error.
   */
  size_t getInflightRequestCount() const;

  /**
   * Update connect and write timeouts. If the new value is larger than the
   * current value, it is ignored.
   *
   * @param connectTimeout  The new connect timeout.
   * @param writeTimeout    The new write timeout.
   */
  void updateTimeoutsIfShorter(
      std::chrono::milliseconds connectTimeout,
      std::chrono::milliseconds writeTimeout);

  /**
   * @return        The transport used to manage socket
   */
  const folly::AsyncTransportWrapper* getTransport();

  /**
   * @return Retransmits per packet used to detect lossy connections
   */
  double getRetransmissionInfo();

  /**
   * Set external queue for managing flush callbacks. By default we'll use
   * EventBase as a manager of these callbacks.
   */
  void setFlushList(FlushList* flushList) {
    base_->setFlushList(flushList);
  }

  /**
   * The name of this transport.
   */
  static constexpr folly::StringPiece name() {
    return "AsyncMcClient";
  }

  /**
   * Tells whether or not this Transport is compatible with the given protocol.
   */
  static constexpr bool isCompatible(mc_protocol_t protocol);

 private:
  std::shared_ptr<AsyncMcClientImpl> base_;
};
} // namespace memcache
} // namespace facebook

#include "AsyncMcClient-inl.h"
