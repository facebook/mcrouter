/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <array>
#include <memory>
#include <string>

#include <folly/IntrusiveList.h>
#include <folly/Range.h>
#include <folly/SpinLock.h>

#include "mcrouter/ProxyDestinationBase.h"
#include "mcrouter/TkoLog.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {

struct AccessPoint;
class AsyncMcClient;
struct RpcStatsContext;

namespace mcrouter {

class ProxyBase;
class ProxyDestinationMap;
class TkoTracker;

struct DestinationRequestCtx {
  int64_t startTime{0};
  int64_t endTime{0};

  explicit DestinationRequestCtx(int64_t now) : startTime(now) {}
};

class ProxyDestination : public ProxyDestinationBase {
 public:
  ~ProxyDestination();

  /**
   * Sends a request to this destination.
   * NOTE: This is a blocking call that will return reply, once it's ready.
   *
   * @param request             The request to send.
   * @param requestContext      Context about this request.
   * @param timeout             The timeout of this call.
   * @param rpcStatsContext     Output argument with stats about the RPC
   */
  template <class Request>
  ReplyT<Request> send(
      const Request& request,
      DestinationRequestCtx& requestContext,
      std::chrono::milliseconds timeout,
      RpcStatsContext& rpcStatsContext);

  void resetInactive();

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

  /**
   * Gracefully closes the connection, allowing it to properly drain if
   * possible.
   */
  void closeGracefully();

 protected:
  void updateTransportTimeoutsIfShorter(
      std::chrono::milliseconds shortestConnectTimeout,
      std::chrono::milliseconds shortestWriteTimeout) override final;
  carbon::Result sendProbe() override final;
  std::weak_ptr<ProxyDestinationBase> selfPtr() override final {
    return selfPtr_;
  }
  void markAsActive() override final;

 private:
  std::unique_ptr<AsyncMcClient> client_;
  // Ensure proxy thread doesn't reset AsyncMcClient
  // while config and stats threads may be accessing it
  mutable folly::SpinLock clientLock_;

  // Retransmits control information
  uint64_t lastRetransCycles_{0}; // Cycles when restransmits were last fetched
  uint64_t rxmitsToCloseConnection_{0};
  uint64_t lastConnCloseCycles_{0}; // Cycles when connection was last closed

  static std::shared_ptr<ProxyDestination> create(
      ProxyBase& proxy,
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);

  AsyncMcClient& getAsyncMcClient();
  void initializeAsyncMcClient();

  ProxyDestination(
      ProxyBase& proxy,
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);

  // Process tko, stats and duration timer.
  void onReply(
      const carbon::Result result,
      DestinationRequestCtx& destreqCtx,
      const RpcStatsContext& rpcStatsContext,
      bool isRequestBufferDirty);

  void handleRxmittingConnection(const carbon::Result result, uint64_t latency);
  bool latencyAboveThreshold(uint64_t latency);

  void* stateList_{nullptr};
  folly::IntrusiveListHook stateListHook_;

  std::weak_ptr<ProxyDestination> selfPtr_;

  friend class ProxyDestinationMap;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "ProxyDestination-inl.h"
