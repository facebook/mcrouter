/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>

#include <gtest/gtest.h>

#include <folly/experimental/fibers/EventBaseLoopController.h>
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/FileUtil.h>
#include <folly/io/async/EventBase.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/lib/network/test/ListenSocket.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::test;

void serverShutdownTest(SSLContextProvider ssl) {
  auto server = TestServer::create(false, ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_ascii_protocol, ssl);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, serverShutdown) {
  serverShutdownTest(noSsl());
}

TEST(AsyncMcClient, serverShutdownSsl) {
  serverShutdownTest(validSsl());
}

void simpleAsciiTimeoutTest(SSLContextProvider ssl) {
  auto server = TestServer::create(false, ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_ascii_protocol, ssl);
  client.sendGet("nohold1", mc_res_found);
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("nohold2", mc_res_timeout);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, simpleAsciiTimeout) {
  simpleAsciiTimeoutTest(noSsl());
}

TEST(AsyncMcClient, simpleAsciiTimeoutSsl) {
  simpleAsciiTimeoutTest(validSsl());
}

void simpleUmbrellaTimeoutTest(SSLContextProvider ssl) {
  auto server = TestServer::create(true, ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_umbrella_protocol, ssl);
  client.sendGet("nohold1", mc_res_found);
  client.sendGet("hold", mc_res_timeout);
  client.sendGet("nohold2", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, simpleUmbrellaTimeout) {
  simpleUmbrellaTimeoutTest(noSsl());
}

TEST(AsyncMcClient, simpleUmbrellaTimeoutSsl) {
  simpleUmbrellaTimeoutTest(validSsl());
}

void noServerTimeoutTest(SSLContextProvider ssl) {
  TestClient client("100::", 11302, 200, mc_ascii_protocol, ssl);
  client.sendGet("hold", mc_res_connect_timeout);
  client.waitForReplies();
}

TEST(AsyncMcClient, noServerTimeout) {
  noServerTimeoutTest(noSsl());
}

TEST(AsyncMcClient, noServerTimeoutSsl) {
  noServerTimeoutTest(validSsl());
}

void immediateConnectFailTest(SSLContextProvider ssl) {
  TestClient client("255.255.255.255", 12345, 200, mc_ascii_protocol, ssl);
  client.sendGet("nohold", mc_res_connect_error);
  client.waitForReplies();
}

TEST(AsyncMcClient, immeadiateConnectFail) {
  immediateConnectFailTest(noSsl());
}

TEST(AsyncMcClient, immeadiateConnectFailSsl) {
  immediateConnectFailTest(validSsl());
}

void testCerts(std::string name, SSLContextProvider ssl, size_t numConns) {
  bool loggedFailure = false;
  failure::addHandler({
    name,
    [&loggedFailure](folly::StringPiece, int, folly::StringPiece,
                     folly::StringPiece, folly::StringPiece msg,
                     const std::map<std::string, std::string>&) {
      if (msg.contains("SSLError")) {
        loggedFailure = true;
      }
    }
  });
  SCOPE_EXIT {
    failure::removeHandler(name);
  };
  auto server = TestServer::create(true, true);
  TestClient brokenClient("localhost", server->getListenPort(), 200,
                          mc_umbrella_protocol, ssl);
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_umbrella_protocol, validSsl());
  brokenClient.sendGet("test", mc_res_connect_error);
  brokenClient.waitForReplies();
  EXPECT_TRUE(loggedFailure);
  client.sendGet("test", mc_res_found);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(numConns, server->getAcceptedConns());
}

TEST(AsyncMcClient, invalidCerts) {
  testCerts("test-invalidCerts", invalidSsl(), 1);
}

TEST(AsyncMcClient, brokenCerts) {
  testCerts("test-brokenCerts", brokenSsl(), 2);
}

void inflightThrottleTest(SSLContextProvider ssl) {
  auto server = TestServer::create(false, ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_ascii_protocol, ssl);
  client.setThrottle(5, 6);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_timeout);
  }
  client.waitForReplies();
  EXPECT_EQ(5, client.getMaxPendingReqs());
  EXPECT_EQ(5, client.getMaxInflightReqs());
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, inflightThrottle) {
  inflightThrottleTest(noSsl());
}

TEST(AsyncMcClient, inflightThrottleSsl) {
  inflightThrottleTest(validSsl());
}

void inflightThrottleFlushTest(SSLContextProvider ssl) {
  auto server = TestServer::create(false, ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_ascii_protocol, ssl);
  client.setThrottle(6, 6);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_found);
  }
  client.sendGet("flush", mc_res_found);
  client.waitForReplies();
  EXPECT_EQ(6, client.getMaxPendingReqs());
  EXPECT_EQ(6, client.getMaxInflightReqs());
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, inflightThrottleFlush) {
  inflightThrottleFlushTest(noSsl());
}

TEST(AsyncMcClient, inflightThrottleFlushSsl) {
  inflightThrottleFlushTest(validSsl());
}

void outstandingThrottleTest(SSLContextProvider ssl) {
  auto server = TestServer::create(false, ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_ascii_protocol, ssl);
  client.setThrottle(5, 5);
  for (size_t i = 0; i < 5; ++i) {
    client.sendGet("hold", mc_res_timeout);
  }
  client.sendGet("flush", mc_res_local_error);
  client.waitForReplies();
  EXPECT_EQ(5, client.getMaxPendingReqs());
  EXPECT_EQ(5, client.getMaxInflightReqs());
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, outstandingThrottle) {
  outstandingThrottleTest(noSsl());
}

TEST(AsyncMcClient, outstandingThrottleSsl) {
  outstandingThrottleTest(validSsl());
}

void connectionErrorTest(SSLContextProvider ssl) {
  auto server = TestServer::create(false, ssl != noSsl());
  TestClient client1("localhost", server->getListenPort(), 200,
                     mc_ascii_protocol, ssl);
  TestClient client2("localhost", server->getListenPort(), 200,
                     mc_ascii_protocol, ssl);
  client1.sendGet("shutdown", mc_res_notfound);
  client1.waitForReplies();
  /* sleep override */ usleep(10000);
  client2.sendGet("test", mc_res_connect_error);
  client2.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, connectionError) {
  connectionErrorTest(noSsl());
}

TEST(AsyncMcClient, connectionErrorSsl) {
  connectionErrorTest(validSsl());
}

void basicTest(mc_protocol_t protocol,
               SSLContextProvider ssl,
               uint64_t qosClass = 0,
               uint64_t qosPath = 0) {
  auto server = TestServer::create(protocol != mc_ascii_protocol,
                                   ssl != noSsl());
  TestClient client("localhost", server->getListenPort(), 200, protocol,
                    ssl, qosClass, qosPath);
  client.sendGet("test1", mc_res_found);
  client.sendGet("test2", mc_res_found);
  client.sendGet("empty", mc_res_found);
  client.sendGet("hold", mc_res_found);
  client.sendGet("test3", mc_res_found);
  client.sendGet("test4", mc_res_found);
  client.sendGet("value_size:4096", mc_res_found);
  client.sendGet("value_size:8192", mc_res_found);
  client.sendGet("value_size:16384", mc_res_found);
  client.waitForReplies(6);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, basicAscii) {
  basicTest(mc_ascii_protocol, noSsl());
}

TEST(AsyncMcClient, basicUmbrella) {
  basicTest(mc_umbrella_protocol, noSsl());
}

TEST(AsyncMcClient, basicCaret) {
  basicTest(mc_caret_protocol, noSsl());
}

TEST(AsyncMcClient, basicAsciiSsl) {
  basicTest(mc_ascii_protocol, validSsl());
}

TEST(AsyncMcClient, basicUmbrellaSsl) {
  basicTest(mc_umbrella_protocol, validSsl());
}

TEST(AsyncMcClient, basicCaretSsl) {
  basicTest(mc_caret_protocol, validSsl());
}

void qosTest(mc_protocol_t protocol, SSLContextProvider ssl,
             uint64_t qosClass, uint64_t qosPath) {
  basicTest(protocol, ssl, qosClass, qosPath);
}

TEST(AsyncMcClient, qosClass1) {
  qosTest(mc_umbrella_protocol, noSsl(), 1, 0);
}

TEST(AsyncMcClient, qosClass2) {
  qosTest(mc_ascii_protocol, noSsl(), 2, 1);
}

TEST(AsyncMcClient, qosClass3) {
  qosTest(mc_umbrella_protocol, validSsl(), 3, 2);
}

TEST(AsyncMcClient, qosClass4) {
  qosTest(mc_ascii_protocol, validSsl(), 4, 3);
}

void reconnectTest(mc_protocol_t protocol) {
  auto bigValue = genBigValue();

  auto server = TestServer::create(protocol == mc_umbrella_protocol, false);
  TestClient client("localhost", server->getListenPort(), 100, protocol);
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
  server->join();
  EXPECT_EQ(2, server->getAcceptedConns());
}

TEST(AsyncMcClient, reconnectAscii) {
  reconnectTest(mc_ascii_protocol);
}

TEST(AsyncMcClient, reconnectUmbrella) {
  reconnectTest(mc_umbrella_protocol);
}

void reconnectImmediatelyTest(mc_protocol_t protocol) {
  auto bigValue = genBigValue();

  auto server = TestServer::create(protocol == mc_umbrella_protocol, false);
  TestClient client("localhost", server->getListenPort(), 100, protocol);
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
  server->join();
  EXPECT_EQ(2, server->getAcceptedConns());
}

TEST(AsyncMcClient, reconnectImmediatelyAscii) {
  reconnectImmediatelyTest(mc_ascii_protocol);
}

TEST(AsyncMcClient, reconnectImmediatelyUmbrella) {
  reconnectImmediatelyTest(mc_umbrella_protocol);
}

void bigKeyTest(mc_protocol_t protocol) {
  auto server = TestServer::create(protocol == mc_umbrella_protocol, false);
  TestClient client("localhost", server->getListenPort(), 200, protocol);
  constexpr int len = MC_KEY_MAX_LEN_ASCII + 5;
  char key[len] = {0};
  for (int i = 0; i < len - 1; ++i) {
    key[i] = 'A';
  }
  client.sendGet(key, mc_res_bad_key);
  client.waitForReplies();
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
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
  auto eventBase = folly::make_unique<folly::EventBase>();
  auto fiberManager =
    folly::make_unique<folly::fibers::FiberManager>(
      folly::make_unique<folly::fibers::EventBaseLoopController>());
  dynamic_cast<folly::fibers::EventBaseLoopController&>(
    fiberManager->loopController()).attachEventBase(*eventBase);
  bool wasUp = false;
  bool replied = false;
  bool wentDown = false;

  ConnectionOptions opts("100::", 11302, mc_ascii_protocol);
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
    McRequestWithMcOp<mc_op_get> req("hold");
    auto reply = client->sendSync(req, std::chrono::milliseconds(100));
    EXPECT_STREQ(mc_res_to_string(reply.result()),
                 mc_res_to_string(mc_res_connect_timeout));
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
  auto server = TestServer::create(false /* outOfOrder */, false /* useSsl */);
  TestClient client("localhost", server->getListenPort(), 200,
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
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, asciiPendingTimeouts) {
  auto server = TestServer::create(false /* outOfOrder */, false /* useSsl */);
  TestClient client("localhost", server->getListenPort(), 200,
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
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, asciiSendingTimeouts) {
  auto bigValue = genBigValue();
  auto server = TestServer::create(false /* outOfOrder */, false /* useSsl */);
  // Use very large write timeout, so that we never timeout writes.
  TestClient client("localhost", server->getListenPort(), 10000,
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
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, oooUmbrellaTimeouts) {
  auto server = TestServer::create(true /* outOfOrder */, false /* useSsl */);
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_umbrella_protocol);
  // Allow only up to two requests in flight.
  client.setThrottle(2, 0);
  client.sendGet("sleep", mc_res_timeout, 500);
  client.sendGet("sleep", mc_res_timeout, 100);
  client.waitForReplies();

  // wait for server to wake up
  /* sleep override */ usleep(3000000);

  client.sendGet("test", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, tonsOfConnections) {
  auto server = TestServer::create(false /* outOfOrder */,
                                   false /* useSsl */,
                                   10,
                                   250,
                                   3 /* maxConns */);

  bool wentDown = false;

  /* Create a client to see if it gets evicted. */
  TestClient client("localhost", server->getListenPort(), 1,
                    mc_ascii_protocol);
  client.setStatusCallbacks([]{}, [&wentDown](bool) { wentDown = true; });
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Create 3 more clients to evict the first client. */
  TestClient client2("localhost", server->getListenPort(), 200,
                     mc_ascii_protocol);
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();
  TestClient client3("localhost", server->getListenPort(), 300,
                     mc_ascii_protocol);
  client3.sendGet("test", mc_res_found);
  client3.waitForReplies();
  TestClient client4("localhost", server->getListenPort(), 400,
                     mc_ascii_protocol);
  client4.sendGet("test", mc_res_found);
  client4.waitForReplies();

  /* Force the status callback to be invoked to see if it was evicted. */
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Should be evicted. */
  EXPECT_TRUE(wentDown);

  /* Given there are at max 3 connections,
   * this should work iff unreapableTime is small (which it is). */
  client4.sendGet("shutdown", mc_res_notfound);
  client4.waitForReplies();

  server->join();
}

void umbrellaBinaryReply(std::string data, mc_res_t expectedResult) {
  ListenSocket sock;

  std::thread serverThread([&sock, &data] {
    auto sockFd = ::accept(sock.getSocketFd(), nullptr, nullptr);
    // Don't read anything, just reply with a serialized reply.
    size_t n = folly::writeFull(sockFd, data.data(), data.size());
    CHECK(n == data.size());
  });

  TestClient client("localhost", sock.getPort(), 200, mc_umbrella_protocol);
  client.sendGet("test", expectedResult);
  client.waitForReplies();
  serverThread.join();
}

TEST(AsyncMcClient, binaryUmbrellaReply) {
  // This is a serialized umbrella reply for get operation with
  // mc_res_notfound result and reqid = 1.
  std::string data
    {'}', '\000', '\000', '\003', '\000', '\000', '\000', ',', '\000', '\001',
     '\000', '\001', '\000', '\000', '\000', '\000', '\000', '\000', '\000',
     '\005', '\000', '\004', '\000', '\004', '\000', '\000', '\000', '\000',
     '\000', '\000', '\000', '\001', '\000', '\001', '\000', '\002', '\000',
     '\000', '\000', '\000', '\000', '\000', '\000', '\003'};

  umbrellaBinaryReply(data, mc_res_notfound);
}

TEST(AsyncMcClient, curruptedUmbrellaReply) {
  // This is a serialized umbrella reply for get operation with
  // reqid = 1, it contains invalid result code (771).
  std::string data
    {'}', '\000', '\000', '\003', '\000', '\000', '\000', ',', '\000', '\001',
     '\000', '\001', '\000', '\000', '\000', '\000', '\000', '\000', '\000',
     '\005', '\000', '\004', '\000', '\004', '\000', '\000', '\000', '\000',
     '\000', '\000', '\000', '\001', '\000', '\001', '\000', '\002', '\000',
     '\000', '\000', '\000', '\000', '\000', '\003', '\003'};

  umbrellaBinaryReply(data, mc_res_remote_error);
}

TEST(AsyncMcClient, SslSessionCache) {
  auto server =
      TestServer::create(true, true, 10, 250, 100, true, 4 /* nThreads */);
  auto constexpr nConnAttempts = 10;

  for (int i = 0; i < nConnAttempts; i++) {
    TestClient client("::1",
                      server->getListenPort(),
                      200, mc_umbrella_protocol,
                      validSsl());
    LOG(INFO) << "Connection attempt: " << i;
    client.sendGet("test", mc_res_found);
    client.waitForReplies();
    auto transport = client.getClient().getTransport();
    auto* socket = transport->getUnderlyingTransport<folly::AsyncSSLSocket>();
    if (i != 0) {
      EXPECT_TRUE(socket->getSSLSessionReused());
    } else {
      EXPECT_FALSE(socket->getSSLSessionReused());
    }
    if (i == nConnAttempts - 1) {
      client.sendGet("shutdown", mc_res_notfound);
      client.waitForReplies();
    }
  }
  server->join();
}

void versionTest(mc_protocol_t protocol, bool useDefaultVersion) {
  auto server = TestServer::create(protocol != mc_ascii_protocol /* OOO */,
                                   false /* useSsl */,
                                   10    /* maxInflight */,
                                   200   /* timeoutMs */,
                                   10    /* maxConns */,
                                   useDefaultVersion);
  TestClient client("localhost", server->getListenPort(), 200, protocol);

  client.sendVersion(server->version());
  client.waitForReplies();
  server->shutdown();
  server->join();
}

TEST(AsyncMcClient, asciiVersionDefault) {
  versionTest(mc_ascii_protocol, true);
}

TEST(AsyncMcClient, asciiVersionUserSpecified) {
  versionTest(mc_ascii_protocol, false);
}

TEST(AsyncMcClient, umbrellaVersionDefault) {
  versionTest(mc_umbrella_protocol, true);
}

TEST(AsyncMcClient, umbrellaVersionUserSpecified) {
  versionTest(mc_umbrella_protocol, false);
}

TEST(AsyncMcClient, caretVersionDefault) {
  versionTest(mc_caret_protocol, true);
}

TEST(AsyncMcClient, caretVersionUserSpecified) {
  versionTest(mc_caret_protocol, false);
}

TEST(AsyncMcClient, caretAdditionalFields) {
  auto server = TestServer::create(true /* OOO */, false /* useSsl */);
  TestClient client("localhost", server->getListenPort(), 200,
                    mc_caret_protocol);
  // Mix in some normal get requests
  for (int i = 0; i < 1000; ++i) {
    client.sendGet("trace_id", mc_res_found);
    client.sendGet("test", mc_res_found);
  }
  client.waitForReplies();
  server->shutdown();
  server->join();
}
