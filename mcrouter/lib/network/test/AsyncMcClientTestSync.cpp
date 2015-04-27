/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/experimental/fibers/EventBaseLoopController.h>
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/network/test/TestUtil.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

using folly::EventBase;

namespace {

struct CommonStats {
  std::atomic<int> accepted{0};
};

const char* kPemKeyPath = "mcrouter/lib/network/test/test_key.pem";
const char* kPemCertPath = "mcrouter/lib/network/test/test_cert.pem";
const char* kPemCaPath = "mcrouter/lib/network/test/ca_cert.pem";

class ServerOnRequest {
 public:
  ServerOnRequest(bool& shutdown,
                  bool outOfOrder) :
      shutdown_(shutdown),
      outOfOrder_(outOfOrder) {
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<mc_op_get>) {
    if (req.fullKey() == "sleep") {
      /* sleep override */ usleep(1000000);
      processReply(std::move(ctx), McReply(mc_res_notfound));
    } else if (req.fullKey() == "shutdown") {
      shutdown_ = true;
      processReply(std::move(ctx), McReply(mc_res_notfound));
      flushQueue();
    } else {
      std::string value = req.fullKey() == "empty" ? "" : req.fullKey().str();
      McReply foundReply = McReply(mc_res_found, createMcMsgRef(req.fullKey(),
                                                                value));
      if (req.fullKey() == "hold") {
        waitingReplies_.emplace_back(std::move(ctx), std::move(foundReply));
      } else if (req.fullKey() == "flush") {
        processReply(std::move(ctx), std::move(foundReply));
        flushQueue();
      } else {
        processReply(std::move(ctx), std::move(foundReply));
      }
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<mc_op_set>) {
    processReply(std::move(ctx), McReply(mc_res_stored));
  }

  template <int M>
  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<M>) {
    LOG(ERROR) << "Unhandled operation " << M;
  }

  void processReply(McServerRequestContext&& context, McReply&& reply) {
    if (outOfOrder_) {
      McServerRequestContext::reply(std::move(context), std::move(reply));
    } else {
      waitingReplies_.emplace_back(std::move(context), std::move(reply));
      if (waitingReplies_.size() == 1) {
        flushQueue();
      }
    }
  }

  void flushQueue() {
    for (size_t i = 0; i < waitingReplies_.size(); ++i) {
      McServerRequestContext::reply(std::move(waitingReplies_[i].first),
                                    std::move(waitingReplies_[i].second));
    }
    waitingReplies_.clear();
  }

 private:
  bool& shutdown_;
  bool outOfOrder_;
  std::vector<std::pair<McServerRequestContext, McReply>> waitingReplies_;
};

class TestServer {
 public:
  TestServer(bool outOfOrder, bool useSsl,
             int maxInflight = 10, int timeoutMs = 250) :
      outOfOrder_(outOfOrder) {
    socketFd_ = createListenSocket();
    opts_.existingSocketFd = socketFd_;
    opts_.numThreads = 1;
    opts_.worker.maxInFlight = maxInflight;
    opts_.worker.sendTimeout = std::chrono::milliseconds{timeoutMs};
    if (useSsl) {
      opts_.pemKeyPath = kPemKeyPath;
      opts_.pemCertPath = kPemCertPath;
      opts_.pemCaPath = kPemCaPath;
    }
    EXPECT_TRUE(run());
    // allow server some time to startup
    /* sleep override */ usleep(100000);
  }

  uint16_t getListenPort() const {
    return facebook::memcache::getListenPort(socketFd_);
  }

  bool run() {
    try {
      LOG(INFO) << "Spawning AsyncMcServer";

      server_ = folly::make_unique<AsyncMcServer>(opts_);
      server_->spawn(
        [this] (size_t threadId,
                folly::EventBase& evb,
                AsyncMcServerWorker& worker) {

          bool shutdown = false;
          worker.setOnRequest(ServerOnRequest(shutdown, outOfOrder_));
          worker.setOnConnectionAccepted([this] () {
            ++stats_.accepted;
          });

          while (!shutdown) {
            evb.loopOnce();
          }

          LOG(INFO) << "Shutting down AsyncMcServer";

          worker.shutdown();
        });

      return true;
    } catch (const folly::AsyncSocketException& e) {
      LOG(ERROR) << e.what();
      return false;
    }
  }

  void join() {
    server_->join();
  }

  CommonStats& getStats() {
    return stats_;
  }
 private:
  int socketFd_;
  AsyncMcServer::Options opts_;
  std::unique_ptr<AsyncMcServer> server_;
  bool outOfOrder_ = false;
  CommonStats stats_;
};

class TestClient {
 public:
  TestClient(std::string host, uint16_t port, int timeoutMs,
             mc_protocol_t protocol = mc_ascii_protocol,
             bool useSsl = false,
             std::function<
               std::shared_ptr<folly::SSLContext>()
             > contextProvider = nullptr,
             bool enableQoS = false,
             uint64_t qosClass = 0,
             uint64_t qosPath = 0) :
      fm_(folly::make_unique<folly::fibers::EventBaseLoopController>()) {
    dynamic_cast<folly::fibers::EventBaseLoopController&>(fm_.loopController()).
      attachEventBase(eventBase_);
    ConnectionOptions opts(host, port, protocol);
    opts.writeTimeout = std::chrono::milliseconds(timeoutMs);
    if (useSsl) {
      auto defaultContextProvider = [] () {
        return getSSLContext(kPemCertPath, kPemKeyPath, kPemCaPath);
      };
      opts.sslContextProvider = contextProvider
        ? contextProvider
        : defaultContextProvider;
    }
    if (enableQoS) {
      opts.enableQoS = true;
      opts.qosClass = qosClass;
      opts.qosPath = qosPath;
    }
    client_ = folly::make_unique<AsyncMcClient>(eventBase_, opts);
    client_->setStatusCallbacks([] { LOG(INFO) << "Client UP."; },
                                [] (bool) { LOG(INFO) << "Client DOWN."; });
  }

  void setThrottle(size_t maxInflight, size_t maxOutstanding) {
    client_->setThrottle(maxInflight, maxOutstanding);
  }

  void sendGet(const char* key, mc_res_t expectedResult) {
    inflight_++;
    std::string K(key);
    fm_.addTask([K, expectedResult, this]() {
        auto msg = createMcMsgRef(K.c_str());
        msg->op = mc_op_get;
        McRequest req{std::move(msg)};
        try {
          auto reply = client_->sendSync(req, McOperation<mc_op_get>(),
                                         std::chrono::milliseconds(200));
          if (reply.result() == mc_res_found) {
            if (req.fullKey() == "empty") {
              EXPECT_TRUE(reply.hasValue());
              EXPECT_EQ("", toString(reply.value()));
            } else {
              EXPECT_EQ(toString(reply.value()), req.fullKey());
            }
          }
          EXPECT_EQ(std::string(mc_res_to_string(expectedResult)),
                    std::string(mc_res_to_string(reply.result())));
        } catch (const std::exception& e) {
          LOG(ERROR) << e.what();
          CHECK(false);
        }
        inflight_--;
      });
  }

  void sendSet(const char* key, const char* value, mc_res_t expectedResult) {
    inflight_++;
    std::string K(key);
    std::string V(value);
    fm_.addTask([K, V, expectedResult, this]() {
        auto msg = createMcMsgRef(K.c_str(), V.c_str());
        msg->op = mc_op_set;
        McRequest req{std::move(msg)};

        auto reply = client_->sendSync(req, McOperation<mc_op_set>(),
                                       std::chrono::milliseconds(200));

        EXPECT_EQ(std::string(mc_res_to_string(expectedResult)),
                  std::string(mc_res_to_string(reply.result())));

        inflight_--;
      });

  }

  size_t getOutstandingCount() const {
    return inflight_;
  }

  /**
   * Wait until there're more than remaining requests in queue.
   */
  void waitForReplies(size_t remaining = 0) {
    while (getOutstandingCount() > remaining) {
      loopOnce();
    }
  }

  /**
   * Loop once client EventBase, will cause it to write requests into socket.
   */
  void loopOnce() {
    eventBase_.loopOnce();
  }

  AsyncMcClient& getClient() {
    return *client_;
  }

  EventBase eventBase_;
 private:
  size_t inflight_{0};
  std::unique_ptr<AsyncMcClient> client_;
  folly::fibers::FiberManager fm_;
};

std::string genBigValue() {
  const size_t kBigValueSize = 1024 * 1024 * 4;
  std::string bigValue(kBigValueSize, '.');
  for (size_t i = 0; i < kBigValueSize; ++i) {
    bigValue[i] = 65 + (i % 26);
  }
  return bigValue;
}

}  // namespace

void serverShutdownTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, serverShutdown) {
  serverShutdownTest();
}

TEST(AsyncMcClient, serverShutdownSsl) {
  serverShutdownTest(true);
}

void simpleAsciiTimeoutTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.sendGet("nohold1", mc_res_found);
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("nohold2", mc_res_timeout);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, simpleAsciiTimeout) {
  simpleAsciiTimeoutTest();
}

TEST(AsyncMcClient, simpleAsciiTimeoutSsl) {
  simpleAsciiTimeoutTest(true);
}

void simpleUmbrellaTimeoutTest(bool useSsl = false) {
  TestServer server(true, useSsl);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_umbrella_protocol, useSsl);
  client.sendGet("nohold1", mc_res_found);
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("nohold2", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, simpleUmbrellaTimeout) {
  simpleUmbrellaTimeoutTest();
}

TEST(AsyncMcClient, simpleUmbrellaTimeoutSsl) {
  simpleUmbrellaTimeoutTest(true);
}

void noServerTimeoutTest(bool useSsl = false) {
  TestClient client("10.1.1.1", 11302, 200, mc_ascii_protocol, useSsl);
  client.sendGet("hold", mc_res_connect_timeout);
  client.waitForReplies();
}

TEST(AsyncMcClient, noServerTimeout) {
  noServerTimeoutTest();
}

TEST(AsyncMcClient, noServerTimeoutSsl) {
  noServerTimeoutTest(true);
}

void immediateConnectFailTest(bool useSsl = false) {
  TestClient client("255.255.255.255", 12345, 200, mc_ascii_protocol, useSsl);
  client.sendGet("nohold", mc_res_connect_error);
  client.waitForReplies();
}

TEST(AsyncMcClient, immeadiateConnectFail) {
  immediateConnectFailTest();
}

TEST(AsyncMcClient, immeadiateConnectFailSsl) {
  immediateConnectFailTest(true);
}

TEST(AsyncMcClient, invalidCerts) {
  TestServer server(true, true);
  TestClient brokenClient("localhost", server.getListenPort(), 200,
                    mc_umbrella_protocol, true, []() {
                      return getSSLContext("/does/not/exist",
                                           "/does/not/exist",
                                           "/does/not/exist");
                    });
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_umbrella_protocol, true);
  brokenClient.sendGet("test", mc_res_connect_error);
  brokenClient.waitForReplies();
  client.sendGet("test", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

void inflightThrottleTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.setThrottle(5, 6);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_timeout);
  }
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, inflightThrottle) {
  inflightThrottleTest();
}

TEST(AsyncMcClient, inflightThrottleSsl) {
  inflightThrottleTest(true);
}

void inflightThrottleFlushTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.setThrottle(6, 6);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_found);
  }
  client.sendGet("flush", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, inflightThrottleFlush) {
  inflightThrottleFlushTest();
}

TEST(AsyncMcClient, inflightThrottleFlushSsl) {
  inflightThrottleFlushTest(true);
}

void outstandingThrottleTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.setThrottle(5, 5);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_timeout);
  }
  client.sendGet("flush", mc_res_local_error);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, outstandingThrottle) {
  outstandingThrottleTest();
}

TEST(AsyncMcClient, outstandingThrottleSsl) {
  outstandingThrottleTest(true);
}

void connectionErrorTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client1("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  TestClient client2("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client1.sendGet("shutdown", mc_res_notfound);
  client1.waitForReplies();
  /* sleep override */ usleep(10000);
  client2.sendGet("test", mc_res_connect_error);
  client2.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, connectionError) {
  connectionErrorTest();
}

TEST(AsyncMcClient, connectionErrorSsl) {
  connectionErrorTest(true);
}

void basicTest(mc_protocol_e protocol = mc_ascii_protocol,
               bool useSsl = false,
               bool enableQoS = false,
               uint64_t qosClass = 0,
               uint64_t qosPath = 0) {
  TestServer server(true, useSsl);
  TestClient client("localhost", server.getListenPort(), 200, protocol,
                    useSsl, nullptr, enableQoS, qosClass, qosPath);
  client.sendGet("test1", mc_res_found);
  client.sendGet("test2", mc_res_found);
  client.sendGet("empty", mc_res_found);
  client.sendGet("hold", mc_res_found);
  client.sendGet("test3", mc_res_found);
  client.sendGet("test4", mc_res_found);
  client.waitForReplies(3);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

void umbrellaTest(bool useSsl = false) {
  basicTest(mc_umbrella_protocol, useSsl);
}

TEST(AsyncMcClient, umbrella) {
  umbrellaTest();
}

TEST(AsyncMcClient, umbrellaSsl) {
  umbrellaTest(true);
}

void qosTest(mc_protocol_e protocol = mc_ascii_protocol, bool useSsl = false,
             uint64_t qosClass = 0, uint64_t qosPath = 0) {
  basicTest(protocol, useSsl, true, qosClass, qosPath);
}

TEST(AsyncMcClient, qosClass1) {
  qosTest(mc_umbrella_protocol, false, 1, 0);
}

TEST(AsyncMcClient, qosClass2) {
  qosTest(mc_ascii_protocol, false, 2, 1);
}

TEST(AsyncMcClient, qosClass3) {
  qosTest(mc_umbrella_protocol, true, 3, 2);
}

TEST(AsyncMcClient, qosClass4) {
  qosTest(mc_ascii_protocol, true, 4, 3);
}

void reconnectTest(mc_protocol_t protocol) {
  auto bigValue = genBigValue();

  TestServer server(protocol == mc_umbrella_protocol, false);
  TestClient client("localhost", server.getListenPort(), 100,
                    protocol);
  client.sendGet("test1", mc_res_found);
  client.sendSet("test", "testValue", mc_res_stored);
  client.waitForReplies();
  client.sendGet("sleep", mc_res_timeout);
  // Wait for the reply, we will still have ~900ms for the write to fail.
  client.waitForReplies();
  client.sendSet("testKey", bigValue.data(), mc_res_remote_error);
  client.waitForReplies();
  // Allow server some time to wake up.
  /* sleep override */ usleep(1000000);
  client.sendGet("test2", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 2);
}

TEST(AsyncMcClient, reconnectAscii) {
  reconnectTest(mc_ascii_protocol);
}

TEST(AsyncMcClient, reconnectUmbrella) {
  reconnectTest(mc_umbrella_protocol);
}

void reconnectImmediatelyTest(mc_protocol_t protocol) {
  auto bigValue = genBigValue();

  TestServer server(protocol == mc_umbrella_protocol, false);
  TestClient client("localhost", server.getListenPort(), 100,
                    protocol);
  client.sendGet("test1", mc_res_found);
  client.sendSet("test", "testValue", mc_res_stored);
  client.waitForReplies();
  client.sendGet("sleep", mc_res_timeout);
  // Wait for the reply, we will still have ~900ms for the write to fail.
  client.waitForReplies();
  // Prevent get from being sent before we reconnect, this will trigger
  // a reconnect in error handling path of AsyncMcClient.
  client.setThrottle(1, 0);
  client.sendSet("testKey", bigValue.data(), mc_res_remote_error);
  client.sendGet("test1", mc_res_timeout);
  client.waitForReplies();
  // Allow server some time to wake up.
  /* sleep override */ usleep(1000000);
  client.sendGet("test2", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 2);
}

TEST(AsyncMcClient, reconnectImmediatelyAscii) {
  reconnectImmediatelyTest(mc_ascii_protocol);
}

TEST(AsyncMcClient, reconnectImmediatelyUmbrella) {
  reconnectImmediatelyTest(mc_umbrella_protocol);
}

void bigKeyTest(mc_protocol_t protocol) {
  TestServer server(protocol == mc_umbrella_protocol, false);
  TestClient client("localhost", server.getListenPort(), 200,
                    protocol);
  constexpr int len = MC_KEY_MAX_LEN_ASCII + 5;
  char key[len] = {0};
  for (int i = 0; i < len - 1; ++i) {
    key[i] = 'A';
  }
  client.sendGet(key, mc_res_bad_key);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, badKey) {
  bigKeyTest(mc_ascii_protocol);
}

TEST(AsyncMcClient, eventBaseDestructionWhileConnecting) {
  // In this test we're going to hit next scenario:
  //  1. Try to connect to non-existing server with timeout of 1s.
  //  2. Fail the request because of timeout.
  //  3. Delete EventBase, this in turn should case proper cleanup
  //     in AsyncMcClient.
  auto eventBase = folly::make_unique<EventBase>();
  auto fiberManager =
    folly::make_unique<folly::fibers::FiberManager>(
      folly::make_unique<folly::fibers::EventBaseLoopController>());
  dynamic_cast<folly::fibers::EventBaseLoopController&>(
    fiberManager->loopController()).attachEventBase(*eventBase);
  bool wasUp = false;
  bool replied = false;
  bool wentDown = false;

  ConnectionOptions opts("10.1.1.1", 11302, mc_ascii_protocol);
  opts.writeTimeout = std::chrono::milliseconds(1000);
  auto client = folly::make_unique<AsyncMcClient>(*eventBase, opts);
  client->setStatusCallbacks(
    [&wasUp] {
      wasUp = true;
    },
    [&wentDown] (bool) {
      wentDown = true;
    });

  fiberManager->addTask([&client, &replied] {
    McRequest req("hold");
    auto reply = client->sendSync(req, McOperation<mc_op_get>(),
                                  std::chrono::milliseconds(100));
    EXPECT_EQ(mc_res_to_string(reply.result()),
              mc_res_to_string(mc_res_timeout));
    replied = true;
  });

  while (fiberManager->hasTasks()) {
    eventBase->loopOnce();
  }

  EXPECT_FALSE(wasUp);
  EXPECT_TRUE(replied);

  fiberManager.reset();
  eventBase.reset();

  EXPECT_FALSE(wasUp);
  EXPECT_TRUE(wentDown);
}

TEST(AsyncMcClient, asciiSentTimeouts) {
  TestServer server(false /* outOfOrder */, false /* useSsl */);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol);
  client.sendGet("test", mc_res_found);
  client.waitForReplies();
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("test2", mc_res_timeout);
  // Wait until we timeout everything.
  client.waitForReplies();
  client.sendGet("flush", mc_res_found);
  client.sendGet("test3", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, asciiPendingTimeouts) {
  TestServer server(false /* outOfOrder */, false /* useSsl */);
  TestClient client("localhost", server.getListenPort(), 200,
                    mc_ascii_protocol);
  // Allow only up to two requests in flight.
  client.setThrottle(2, 0);
  client.sendGet("test", mc_res_found);
  client.waitForReplies();
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("test2", mc_res_timeout);
  client.sendGet("test3", mc_res_timeout);
  // Wait until we timeout everything.
  client.waitForReplies();
  client.sendGet("flush", mc_res_found);
  client.sendGet("test3", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, asciiSendingTimeouts) {
  auto bigValue = genBigValue();
  TestServer server(false /* outOfOrder */, false /* useSsl */);
  // Use very large write timeout, so that we never timeout writes.
  TestClient client("localhost", server.getListenPort(), 10000,
                    mc_ascii_protocol);
  // Allow only up to two requests in flight.
  client.sendGet("test", mc_res_found);
  client.waitForReplies();
  client.sendGet("sleep", mc_res_timeout);
  // Wait for the request to timeout.
  client.waitForReplies();
  // We'll need to hold the reply to the set request.
  client.sendGet("hold", mc_res_timeout);
  // Will overfill write queue of the server and timeout before completely
  // written.
  client.sendSet("testKey", bigValue.data(), mc_res_timeout);
  // Wait until we complete send, note this will happen after server wakes up.
  // This is due to the fact that we cannot timeout until the request wasn't
  // completely sent.
  client.waitForReplies();
  // Flush set reply.
  client.sendGet("flush", mc_res_found);
  client.sendGet("test3", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}
