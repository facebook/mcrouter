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

#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"
#include "mcrouter/lib/network/test/TestUtil.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

using folly::EventBase;

void serverShutdownTest(bool useSsl = false) {
  TestServer<TestServerOnRequest> server(false, useSsl);
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
  TestServer<TestServerOnRequest> server(false, useSsl);
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
  TestServer<TestServerOnRequest> server(true, useSsl);
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
  TestClient client("100::", 11302, 200, mc_ascii_protocol, useSsl);
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
  TestServer<TestServerOnRequest> server(true, true);
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
  TestServer<TestServerOnRequest> server(false, useSsl);
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
  TestServer<TestServerOnRequest> server(false, useSsl);
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
  TestServer<TestServerOnRequest> server(false, useSsl);
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
  TestServer<TestServerOnRequest> server(false, useSsl);
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
  TestServer<TestServerOnRequest> server(true, useSsl);
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

  TestServer<TestServerOnRequest> server(protocol == mc_umbrella_protocol,
                                         false);
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

  TestServer<TestServerOnRequest> server(protocol == mc_umbrella_protocol,
                                         false);
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
  TestServer<TestServerOnRequest> server(protocol == mc_umbrella_protocol,
                                         false);
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
    McRequest req("hold");
    auto reply = client->sendSync(req, McOperation<mc_op_get>(),
                                  std::chrono::milliseconds(100));
    EXPECT_STREQ(mc_res_to_string(reply.result()),
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
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */);
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
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */);
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
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */);
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

TEST(AsyncMcClient, oooUmbrellaTimeouts) {
  TestServer<TestServerOnRequest> server(true /* outOfOrder */,
                                         false /* useSsl */);
  TestClient client("localhost", server.getListenPort(), 200,
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
  server.join();
  EXPECT_EQ(server.getStats().accepted.load(), 1);
}

TEST(AsyncMcClient, tonsOfConnections) {
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */,
                                         10,
                                         250,
                                         3 /* maxConns */,
                                         0 /* unreapableTime */);

  bool wentDown = false;

  /* Create a client to see if it gets evicted. */
  TestClient client("localhost", server.getListenPort(), 1,
                    mc_ascii_protocol);
  client.setStatusCallbacks([]{}, [&wentDown](bool) { wentDown = true; });
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Create 3 more clients to evict the first client. */
  TestClient client2("localhost", server.getListenPort(), 200,
                     mc_ascii_protocol);
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();
  TestClient client3("localhost", server.getListenPort(), 300,
                     mc_ascii_protocol);
  client3.sendGet("test", mc_res_found);
  client3.waitForReplies();
  TestClient client4("localhost", server.getListenPort(), 400,
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

  server.join();
}

TEST(AsyncMcClient, disableConnectionLRU) {
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */,
                                         10,
                                         250,
                                         0 /* maxConns */,
                                         1000000 /* unreapableTime */);

  bool wentDown = false;

  /* Create a client to see if it gets evicted. */
  TestClient client("localhost", server.getListenPort(), 1,
                    mc_ascii_protocol);
  client.setStatusCallbacks([]{}, [&wentDown](bool) { wentDown = true; });
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Create 3 more clients to evict the first client. */
  TestClient client2("localhost", server.getListenPort(), 200,
                     mc_ascii_protocol);
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();
  TestClient client3("localhost", server.getListenPort(), 300,
                     mc_ascii_protocol);
  client3.sendGet("test", mc_res_found);
  client3.waitForReplies();
  TestClient client4("localhost", server.getListenPort(), 400,
                     mc_ascii_protocol);
  client4.sendGet("test", mc_res_found);
  client4.waitForReplies();

  /* Force the status callback to be invoked to see if it was evicted. */
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Shouldn't be evicted. */
  EXPECT_FALSE(wentDown);

  /* Given unreapableTime is large, this should work iff LRU is disabled. */
  client4.sendGet("shutdown", mc_res_notfound);
  client4.waitForReplies();

  server.join();
}

TEST(AsyncMcClient, testUnreapableTime) {
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */,
                                         10,
                                         250,
                                         3 /* maxConns */,
                                         1000000 /* unreapableTime */);

  bool firstWentDown = false;
  bool lastWentDown = false;

  /* Create a client to see if it gets evicted. */
  TestClient client("localhost", server.getListenPort(), 1,
                    mc_ascii_protocol);
  client.setStatusCallbacks([]{}, [&firstWentDown](bool) {
    firstWentDown = true;
  });
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Create 3 more clients to evict the first client. */
  TestClient client2("localhost", server.getListenPort(), 200,
                     mc_ascii_protocol);
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();
  TestClient client3("localhost", server.getListenPort(), 300,
                     mc_ascii_protocol);
  client3.sendGet("test", mc_res_found);
  client3.waitForReplies();
  TestClient client4("localhost", server.getListenPort(), 400,
                     mc_ascii_protocol);
  client4.setStatusCallbacks([]{}, [&lastWentDown](bool) {
    lastWentDown = true;
  });
  /* Should be an error. */
  client4.sendGet("test", mc_res_remote_error);
  client4.waitForReplies();

  /* Force the status callback to be invoked to see if it was evicted. */
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* The first connection shouldn't be evicted. */
  EXPECT_FALSE(firstWentDown);

  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  /* The last connection should be closed. */
  EXPECT_TRUE(lastWentDown);

  server.join();
}

TEST(AsyncMcClient, testUpdateThreshold) {
  TestServer<TestServerOnRequest> server(false /* outOfOrder */,
                                         false /* useSsl */,
                                         10,
                                         250,
                                         2 /* maxConns */,
                                         0 /* unreapableTime */,
                                         2000 /* updateTime = 2 sec */);

  bool firstWentDown = false;
  bool secondWentDown = false;

  /* Create a client to see if it gets evicted. */
  TestClient client("localhost", server.getListenPort(), 1,
                    mc_ascii_protocol);
  client.setStatusCallbacks([]{}, [&firstWentDown](bool) {
    firstWentDown = true;
  });
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Now we can update the position of the first connection in the LRU. */
  /* sleep override */ usleep(3000);

  /* Create a second client to see if it gets evicted. */
  TestClient client2("localhost", server.getListenPort(), 200,
                     mc_ascii_protocol);
  client2.setStatusCallbacks([]{}, [&secondWentDown](bool) {
    secondWentDown = true;
  });
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();

  /* Attempt to update the first connection's position in the LRU */
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Attempt to update the second connection's position in the LRU */
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();

  /* Create a third connection to evict one of the first two. */
  TestClient client3("localhost", server.getListenPort(), 200,
                     mc_ascii_protocol);
  client3.sendGet("test", mc_res_found);
  client3.waitForReplies();

  /* Check if either of the initial connections were evicted. */
  client.sendGet("test", mc_res_found);
  client.waitForReplies();
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();

  /* The first connection shouldn't be evicted. */
  EXPECT_FALSE(firstWentDown);
  /* The second connection should be evicted. */
  EXPECT_TRUE(secondWentDown);

  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  server.join();
}

void umbrellaBinaryReply(std::string data, mc_res_t expectedResult) {
  auto serverSockFd = createListenSocket();
  auto listenPort = getListenPort(serverSockFd);

  std::thread serverThread([serverSockFd, &data] {
    auto sockFd = ::accept(serverSockFd, nullptr, nullptr);
    // Don't read anything, just reply with a serialized reply.
    size_t n = folly::writeFull(sockFd, data.data(), data.size());
    CHECK(n == data.size());
  });

  TestClient client("localhost", listenPort, 200, mc_umbrella_protocol);
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
