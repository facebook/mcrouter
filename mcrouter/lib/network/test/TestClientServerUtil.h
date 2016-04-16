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

#include <atomic>
#include <functional>
#include <string>

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/Function.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/test/ListenSocket.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache { namespace test {

class TestServerOnRequest
    : public ThriftMsgDispatcher<TRequestList,
                                 TestServerOnRequest,
                                 McServerRequestContext&&> {
 public:
  TestServerOnRequest(std::atomic<bool>& shutdown, bool outOfOrder);

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_get>&& req);

  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<cpp2::McGetRequest>&& req);

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_set>&& req);

  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<cpp2::McSetRequest>&& req);

  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<cpp2::McVersionRequest>&& req);

  template <class Request>
  void onRequest(McServerRequestContext&&, Request&&) {
    LOG(ERROR) << "Unhandled operation " << typeid(Request).name();
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
  std::atomic<bool>& shutdown_;
  bool outOfOrder_;
  std::vector<folly::Function<void()>> waitingReplies_;

  void flushQueue();
};

class TestServer {
 public:
  template <class OnRequest = TestServerOnRequest>
  static std::unique_ptr<TestServer> create(bool outOfOrder,
                                            bool useSsl,
                                            int maxInflight = 10,
                                            int timeoutMs = 250,
                                            size_t maxConns = 100,
                                            bool useDefaultVersion = false,
                                            size_t numThreads = 1) {
    std::unique_ptr<TestServer> server(new TestServer(
        outOfOrder,
        useSsl,
        maxInflight,
        timeoutMs,
        maxConns,
        useDefaultVersion,
        numThreads));
    server->run([&s = *server](AsyncMcServerWorker& worker) {
      worker.setOnRequest(OnRequest(s.shutdown_, s.outOfOrder_));
    });
    return server;
  }

  uint16_t getListenPort() const {
    return sock_.getPort();
  }

  void join() {
    server_->join();
  }

  size_t getAcceptedConns() const {
    return acceptedConns_.load();
  }

  void shutdown() {
    shutdown_.store(true);
  }

  std::string version() const;

 private:
  ListenSocket sock_;
  AsyncMcServer::Options opts_;
  std::unique_ptr<AsyncMcServer> server_;
  bool outOfOrder_{false};
  std::atomic<bool> shutdown_{false};
  std::atomic<size_t> acceptedConns_{0};

  TestServer(bool outOfOrder, bool useSsl, int maxInflight, int timeoutMs,
             size_t maxConns, bool useDefaultVersion, size_t numThreads);

  void run(std::function<void(AsyncMcServerWorker&)> init);
};

using SSLContextProvider = std::function<std::shared_ptr<folly::SSLContext>()>;

// do not use SSL encryption at all
constexpr std::nullptr_t noSsl() { return nullptr; }
// valid client SSL certs
SSLContextProvider validSsl();
// non-existent client SSL certs
SSLContextProvider invalidSsl();
// broken client SSL certs (handshake fails)
SSLContextProvider brokenSsl();

class TestClient {
 public:
  TestClient(std::string host,
             uint16_t port,
             int timeoutMs,
             mc_protocol_t protocol = mc_ascii_protocol,
             SSLContextProvider ssl = noSsl(),
             uint64_t qosClass = 0,
             uint64_t qosPath = 0);

  void setThrottle(size_t maxInflight, size_t maxOutstanding) {
    client_->setThrottle(maxInflight, maxOutstanding);
  }

  void setStatusCallbacks(std::function<void()> onUp,
                          std::function<void(bool aborting)> onDown);

  void sendGet(std::string key, mc_res_t expectedResult,
               uint32_t timeoutMs = 200);

  void sendSet(std::string key, std::string value, mc_res_t expectedResult);

  void sendVersion(std::string expectedVersion);

  /**
   * Wait until there're more than remaining requests in queue.
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

}}} // facebook::memcache::test
