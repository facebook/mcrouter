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

#include <atomic>
#include <functional>
#include <string>

#include <folly/Function.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/CarbonMessageDispatcher.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/test/ListenSocket.h"

namespace folly {
class AsyncSocket;
} // namespace folly

namespace facebook {
namespace memcache {

class CompressionCodecMap;
struct ReplyStatsContext;

namespace test {

class TestServerOnRequest {
 public:
  TestServerOnRequest(folly::fibers::Baton& shutdownLock, bool outOfOrder);

  void onRequest(McServerRequestContext&& ctx, McGetRequest&& req);
  void onRequest(McServerRequestContext&& ctx, McSetRequest&& req);
  void onRequest(McServerRequestContext&& ctx, McVersionRequest&& req);

  template <class Request>
  void onRequest(McServerRequestContext&&, Request&&) {
    LOG(ERROR) << "Unhandled operation: " << Request::name;
  }

 protected:
  template <class Reply>
  void processReply(McServerRequestContext&& context, Reply&& reply) {
    if (outOfOrder_) {
      McServerRequestContext::reply(std::move(context), std::move(reply));
    } else {
      waitingReplies_.push_back(
          [ctx = std::move(context), reply = std::move(reply)]() mutable {
            McServerRequestContext::reply(std::move(ctx), std::move(reply));
          });
      if (waitingReplies_.size() == 1) {
        flushQueue();
      }
    }
  }

 private:
  folly::fibers::Baton& shutdownLock_;
  bool outOfOrder_;
  std::vector<folly::Function<void()>> waitingReplies_;

  void flushQueue();
};

class TestServer {
 public:
  template <class OnRequest = TestServerOnRequest>
  static std::unique_ptr<TestServer> create(
      bool outOfOrder,
      bool useSsl,
      int maxInflight = 10,
      int timeoutMs = 250,
      size_t maxConns = 100,
      bool useDefaultVersion = false,
      size_t numThreads = 1,
      bool useTicketKeySeeds = false,
      size_t goAwayTimeoutMs = 1000,
      const CompressionCodecMap* compressionCodecMap = nullptr,
      bool tfoEnabled = false) {
    std::unique_ptr<TestServer> server(new TestServer(
        outOfOrder,
        useSsl,
        maxInflight,
        timeoutMs,
        maxConns,
        useDefaultVersion,
        numThreads,
        useTicketKeySeeds,
        goAwayTimeoutMs,
        tfoEnabled));
    server->run(
        [compressionCodecMap, &s = *server](AsyncMcServerWorker& worker) {
          worker.setCompressionCodecMap(compressionCodecMap);
          worker.setOnRequest(MemcacheRequestHandler<OnRequest>(
              s.shutdownLock_, s.outOfOrder_));
        });
    return server;
  }

  uint16_t getListenPort() const {
    return sock_.getPort();
  }

  void join() {
    serverThread_.join();
  }

  size_t getAcceptedConns() const {
    return acceptedConns_.load();
  }

  void shutdown() {
    shutdownLock_.post();
  }

  std::string version() const;

 private:
  ListenSocket sock_;
  AsyncMcServer::Options opts_;
  std::unique_ptr<AsyncMcServer> server_;
  std::thread serverThread_;
  bool outOfOrder_{false};
  bool useTicketKeySeeds_{false};
  folly::fibers::Baton shutdownLock_;
  std::atomic<size_t> acceptedConns_{0};

  TestServer(
      bool outOfOrder,
      bool useSsl,
      int maxInflight,
      int timeoutMs,
      size_t maxConns,
      bool useDefaultVersion,
      size_t numThreads,
      bool useTicketKeySeeds,
      size_t goAwayTimeoutMs,
      bool tfoEnabled);

  void run(std::function<void(AsyncMcServerWorker&)> init);
};

using SSLContextProvider = std::function<std::shared_ptr<folly::SSLContext>()>;

// do not use SSL encryption at all
constexpr std::nullptr_t noSsl() {
  return nullptr;
}
// valid Client SSL Certs
SSLContextProvider validClientSsl();
// valid SSL certs
SSLContextProvider validSsl();
// non-existent client SSL certs
SSLContextProvider invalidSsl();
// broken client SSL certs (handshake fails)
SSLContextProvider brokenSsl();

class TestClient {
 public:
  TestClient(
      std::string host,
      uint16_t port,
      int timeoutMs,
      mc_protocol_t protocol = mc_ascii_protocol,
      SSLContextProvider ssl = noSsl(),
      uint64_t qosClass = 0,
      uint64_t qosPath = 0,
      std::string serviceIdentity = "",
      const CompressionCodecMap* compressionCodecMap = nullptr,
      bool enableTfo = false);

  void setThrottle(size_t maxInflight, size_t maxOutstanding) {
    client_->setThrottle(maxInflight, maxOutstanding);
  }

  void setStatusCallbacks(
      std::function<void(const folly::AsyncSocket&)> onUp,
      std::function<void(AsyncMcClient::ConnectionDownReason)> onDown);

  void sendGet(
      std::string key,
      mc_res_t expectedResult,
      uint32_t timeoutMs = 200,
      std::function<void(const ReplyStatsContext&)> replyStatsCallback =
          nullptr);

  void sendSet(
      std::string key,
      std::string value,
      mc_res_t expectedResult,
      std::function<void(const ReplyStatsContext&)> replyStatsCallback =
          nullptr);

  void sendVersion(std::string expectedVersion);

  /**
   * Wait while there're more than remaining requests in queue.
   */
  void waitForReplies(size_t remaining = 0);

  /**
   * Loop once client EventBase, will cause it to write requests into socket.
   */
  void loopOnce() {
    eventBase_.loopOnce();
  }

  AsyncMcClient& getClient() {
    return *client_;
  }

  /**
   * Get the max number of pending requests at any point in time.
   */
  int getMaxPendingReqs() {
    return pendingStatMax_;
  }

  /**
   * Get the max number of inflight requests at any point in time.
   */
  int getMaxInflightReqs() {
    return inflightStatMax_;
  }

 private:
  size_t inflight_{0};
  std::unique_ptr<AsyncMcClient> client_;
  folly::EventBase eventBase_;
  folly::fibers::FiberManager fm_;

  // Stats
  int pendingStat_{0};
  int inflightStat_{0};
  int pendingStatMax_{0};
  int inflightStatMax_{0};
};

std::string genBigValue();
} // test
} // memcache
} // facebook
