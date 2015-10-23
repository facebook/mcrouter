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

#include <atomic>
#include <functional>

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/test/ListenSocket.h"

namespace facebook { namespace memcache { namespace test {

class TestServerOnRequest {
 public:
  TestServerOnRequest(bool& shutdown, bool outOfOrder);

  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<mc_op_get>);

  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<mc_op_set>);

  template <int M>
  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<M>) {
    LOG(ERROR) << "Unhandled operation " << M;
  }

 protected:
  void processReply(McServerRequestContext&& context, McReply&& reply);

 private:
  bool& shutdown_;
  bool outOfOrder_;
  std::vector<std::pair<McServerRequestContext, McReply>> waitingReplies_;

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
                                            size_t unreapableTime = 0,
                                            size_t updateThreshold = 0) {
    std::unique_ptr<TestServer> server(new TestServer(
      outOfOrder, useSsl, maxInflight, timeoutMs, maxConns,
      unreapableTime, updateThreshold));
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
 private:
  ListenSocket sock_;
  AsyncMcServer::Options opts_;
  std::unique_ptr<AsyncMcServer> server_;
  bool outOfOrder_{false};
  bool shutdown_{false};
  std::atomic<size_t> acceptedConns_{0};

  TestServer(bool outOfOrder, bool useSsl,
             int maxInflight, int timeoutMs, size_t maxConns,
             size_t unreapableTime, size_t updateThreshold);

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
             uint64_t qosPath = 0,
             bool useTyped = false);

  void setThrottle(size_t maxInflight, size_t maxOutstanding) {
    client_->setThrottle(maxInflight, maxOutstanding);
  }

  void setStatusCallbacks(std::function<void()> onUp,
                          std::function<void(bool aborting)> onDown);

  void sendGet(std::string key, mc_res_t expectedResult,
               uint32_t timeoutMs = 200);

  void sendSet(std::string key, std::string value, mc_res_t expectedResult);

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

 private:
  size_t inflight_{0};
  std::unique_ptr<AsyncMcClient> client_;
  folly::EventBase eventBase_;
  folly::fibers::FiberManager fm_;
};

std::string genBigValue();

}}} // facebook::memcache::test
