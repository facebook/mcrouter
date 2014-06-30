/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <gtest/gtest.h>

#include "folly/io/async/EventBase.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "thrift/lib/cpp/transport/TSSLSocket.h"

using folly::EventBase;
using namespace facebook::memcache;

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

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_get>) {
    if (req.fullKey() == "sleep") {
      usleep(1000000);
      processReply(ctx, McReply(mc_res_ok));
    } else if (req.fullKey() == "shutdown") {
      shutdown_ = true;
      processReply(ctx, McReply(mc_res_ok));
      flushQueue();
    } else {
      McReply foundReply = McReply(mc_res_found, createMcMsgRef(req.fullKey(),
                                                                req.fullKey()));
      if (req.fullKey() == "hold") {
        waitingReplies_.emplace_back(ctx, std::move(foundReply));
      } else if (req.fullKey() == "flush") {
        processReply(ctx, std::move(foundReply));
        flushQueue();
      } else {
        processReply(ctx, std::move(foundReply));
      }
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_set>) {
    processReply(ctx, McReply(mc_res_stored));
  }

  template <int M>
  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<M>) {
    LOG(ERROR) << "Unhandled operation " << M;
  }

  void processReply(McServerRequestContext& context, McReply&& reply) {
    if (outOfOrder_) {
      context.sendReply(std::move(reply));
    } else {
      waitingReplies_.emplace_back(context, std::move(reply));
      if (waitingReplies_.size() == 1) {
        flushQueue();
      }
    }
  }

  void flushQueue() {
    for (size_t i = 0; i < waitingReplies_.size(); ++i) {
      waitingReplies_[i].first.sendReply(std::move(waitingReplies_[i].second));
    }
    waitingReplies_.clear();
  }

 private:
  bool& shutdown_;
  bool outOfOrder_;
  std::vector<std::pair<McServerRequestContext&, McReply>> waitingReplies_;
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
    usleep(100000);
  }

  uint16_t getListenPort() const {
    return getListenPort(socketFd_);
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
    } catch (const apache::thrift::transport::TTransportException& e) {
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

  static uint16_t getListenPort(int socketFd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(struct sockaddr_in);
    CHECK(!getsockname(socketFd, (struct sockaddr *)&sin, &len));
    return ntohs(sin.sin_port);
  }

  static int createListenSocket() {
    struct addrinfo hints;
    struct addrinfo* res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    /* Use all available interfaces, and choose an available port */
    CHECK(!getaddrinfo(nullptr, "0", &hints, &res));

    auto listen_socket =
      socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    CHECK(listen_socket >= 0);
    CHECK(!bind(listen_socket, res->ai_addr, res->ai_addrlen));
    CHECK(!listen(listen_socket, SOMAXCONN));

    return listen_socket;
  }
};

class TestClient {
 public:
  TestClient(std::string host, uint16_t port, int timeoutMs,
             mc_protocol_t protocol = mc_ascii_protocol,
             bool useSsl = false) {
    ConnectionOptions opts(host, port, protocol);
    opts.timeout = std::chrono::milliseconds(timeoutMs);
    if (useSsl) {
      auto contextProvider = [] () {
        return getSSLContext(kPemCertPath, kPemKeyPath, kPemCaPath);
      };
      opts.sslContextProvider = contextProvider;
    }
    client_ = folly::make_unique<AsyncMcClient>(eventBase_, opts);
    client_->setStatusCallbacks([] { LOG(INFO) << "Client UP."; },
                                [] (const AsyncMcClient::TransportException&) {
                                  LOG(INFO) << "Client DOWN.";
                                });
  }

  void setThrottle(size_t maxInflight, size_t maxOutstanding) {
    client_->setThrottle(maxInflight, maxOutstanding);
  }

  void sendGet(const char* key, mc_res_t expectedResult) {
    auto msg = createMcMsgRef(key);
    msg->op = mc_op_get;
    auto req = std::make_shared<McRequest>(std::move(msg));
    try {
      client_->send(*req,
                    McOperation<mc_op_get>(),
                    [expectedResult, req, this] (McReply&& reply) {
                      if (reply.result() == mc_res_found) {
                        EXPECT_EQ(toString(reply.value()), req->fullKey());
                      }
                      EXPECT_EQ(expectedResult, reply.result());
                    });
    } catch (const std::exception& e) {
      LOG(ERROR) << e.what();
      CHECK(false);
    }
  }

  void sendSet(const char* key, const char* value, mc_res_t expectedResult) {
    auto msg = createMcMsgRef(key, value);
    msg->op = mc_op_set;
    auto req = std::make_shared<McRequest>(std::move(msg));
    client_->send(*req,
                  McOperation<mc_op_set>(),
                  [expectedResult, req, this] (McReply&& reply) {
                    EXPECT_EQ(expectedResult, reply.result());
                  });
  }

  size_t getOutstandingCount() const {
    return client_->getPendingRequestCount() +
           client_->getInflightRequestCount();
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
  std::unique_ptr<AsyncMcClient> client_;
};

}  // namespace

void serverShutdownTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.sendGet("shutdown", mc_res_ok);
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
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.sendGet("nohold1", mc_res_found);
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("nohold2", mc_res_timeout);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_ok);
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
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_umbrella_protocol, useSsl);
  client.sendGet("nohold1", mc_res_found);
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("nohold2", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_ok);
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

void inflightThrottleTest(bool useSsl = false) {
  TestServer server(false, useSsl);
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.setThrottle(5, 6);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_timeout);
  }
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_ok);
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
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.setThrottle(6, 6);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_found);
  }
  client.sendGet("flush", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_ok);
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
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client.setThrottle(5, 5);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_timeout);
  }
  client.sendGet("flush", mc_res_local_error);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_ok);
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
  TestClient client1("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  TestClient client2("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol, useSsl);
  client1.sendGet("shutdown", mc_res_ok);
  client1.waitForReplies();
  usleep(10000);
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

void umbrellaTest(bool useSsl = false) {
  TestServer server(true, useSsl);
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_umbrella_protocol, useSsl);
  client.sendGet("test1", mc_res_found);
  client.sendGet("test2", mc_res_found);
  client.sendGet("hold", mc_res_found);
  client.sendGet("test3", mc_res_found);
  client.sendGet("test4", mc_res_found);
  client.waitForReplies(3);
  client.sendGet("shutdown", mc_res_ok);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, umbrella) {
  umbrellaTest();
}

TEST(AsyncMcClient, umbrellaSsl) {
  umbrellaTest(true);
}

void reconnectTest(mc_protocol_t protocol) {
  const size_t kBigValueSize = 1024 * 1024 * 4;
  std::string bigValue(kBigValueSize, '.');
  for (size_t i = 0; i < kBigValueSize; ++i) {
    bigValue[i] = 65 + (i % 26);
  }

  TestServer server(protocol == mc_umbrella_protocol, false);
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    protocol);
  client.sendGet("test1", mc_res_found);
  client.sendSet("test", "testValue", mc_res_stored);
  client.waitForReplies();
  client.sendGet("sleep", mc_res_timeout);
  // Wait for the reply, we will still have ~800ms for the write to fail.
  client.waitForReplies();
  client.sendSet("testKey", bigValue.data(), mc_res_remote_error);
  client.waitForReplies();
  // Allow server some time to wake up.
  usleep(1000000);
  client.sendGet("test2", mc_res_found);
  client.sendGet("shutdown", mc_res_ok);
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

void bigKeyTest(mc_protocol_t protocol) {
  TestServer server(protocol == mc_umbrella_protocol, false);
  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    protocol);
  constexpr int len = MC_KEY_MAX_LEN_ASCII + 5;
  char key[len] = {0};
  for (int i = 0; i < len - 1; ++i) {
    key[i] = 'A';
  }
  client.sendGet(key, mc_res_bad_key);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_ok);
  client.waitForReplies();
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, badKey) {
  bigKeyTest(mc_ascii_protocol);
}

// Test that we won't leave zombie AsyncMcClientImpl after EventBase is
// destroyed.
TEST(AsyncMcClient, eventBaseDestruction) {
  TestServer server(false, false);
  bool wasUp = false;
  bool up = false;
  bool replied = false;

  auto msg = createMcMsgRef("sleep");
  msg->op = mc_op_get;
  McRequest req(std::move(msg));

  {
    EventBase eventBase;
    {
      ConnectionOptions opts("127.0.0.1", server.getListenPort(),
                             mc_ascii_protocol);
      opts.timeout = std::chrono::milliseconds(200);
      auto client = folly::make_unique<AsyncMcClient>(eventBase, opts);
      client->setStatusCallbacks(
        [&up, &wasUp] {
          up = wasUp = true;
        },
        [&up] (const AsyncMcClient::TransportException&) {
          up = false;
        });
      client->send(req, McOperation<mc_op_get>(),
                   [&replied] (McReply&& reply) {
                     EXPECT_EQ(reply.result(), mc_res_aborted);
                     replied = true;
                   });
    }
    // This will be triggered before the timeout of client (i.e. it will be
    // processed earlier).
    eventBase.runAfterDelay([&eventBase] {
                              eventBase.terminateLoopSoon();
                            }, 100);
    // Trigger pushMessages() callback.
    eventBase.runInLoop([] {});
    eventBase.loopOnce();
    // Sleep for 300ms, we would have 2 active timeout events in event_base.
    // The first one will case event_base to terminate and will leave one
    // unprocessed active event in queue.
    usleep(300000);
    eventBase.loopOnce();
  }
  EXPECT_TRUE(wasUp);
  EXPECT_FALSE(up);
  EXPECT_TRUE(replied);
  // Wait for server to wake-up.
  usleep(700000);

  TestClient client("127.0.0.1", server.getListenPort(), 200,
                    mc_ascii_protocol);
  client.sendGet("shutdown", mc_res_ok);
  client.waitForReplies();
  server.join();
}
