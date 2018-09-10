/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <string>

#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GFlags.h>

#include "mcrouter/lib/network/McSSLUtil.h"
#include "mcrouter/lib/network/SecurityMech.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/test/ListenSocket.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::test;

namespace folly {
class AsyncSocket;
} // namespace folly

void serverShutdownTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, serverShutdown) {
  serverShutdownTest(folly::none);
}

TEST(AsyncMcClient, serverShutdownSsl) {
  serverShutdownTest(validClientSsl());
}

void simpleAsciiTimeoutTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
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
  simpleAsciiTimeoutTest(folly::none);
}

TEST(AsyncMcClient, simpleAsciiTimeoutSsl) {
  simpleAsciiTimeoutTest(validClientSsl());
}

void simpleUmbrellaTimeoutTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost",
      server->getListenPort(),
      200,
      mc_umbrella_protocol_DONOTUSE,
      ssl);
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
  simpleUmbrellaTimeoutTest(folly::none);
}

TEST(AsyncMcClient, simpleUmbrellaTimeoutSsl) {
  simpleUmbrellaTimeoutTest(validClientSsl());
}

void noServerTimeoutTest(folly::Optional<SSLTestPaths> ssl) {
  TestClient client("100::", 11302, 200, mc_ascii_protocol, ssl);
  client.sendGet("hold", mc_res_connect_timeout);
  client.waitForReplies();
}

TEST(AsyncMcClient, noServerTimeout) {
  noServerTimeoutTest(folly::none);
}

TEST(AsyncMcClient, noServerTimeoutSsl) {
  noServerTimeoutTest(validClientSsl());
}

void immediateConnectFailTest(folly::Optional<SSLTestPaths> ssl) {
  TestClient client("255.255.255.255", 12345, 200, mc_ascii_protocol, ssl);
  client.sendGet("nohold", mc_res_connect_error);
  client.waitForReplies();
}

TEST(AsyncMcClient, immeadiateConnectFail) {
  immediateConnectFailTest(folly::none);
}

TEST(AsyncMcClient, immeadiateConnectFailSsl) {
  immediateConnectFailTest(validClientSsl());
}

void testCerts(
    std::string name,
    folly::Optional<SSLTestPaths> ssl,
    size_t numConns) {
  bool loggedFailure = false;
  failure::addHandler({name,
                       [&loggedFailure](
                           folly::StringPiece,
                           int,
                           folly::StringPiece,
                           folly::StringPiece,
                           folly::StringPiece msg,
                           const std::map<std::string, std::string>&) {
                         if (msg.contains("SSLError")) {
                           loggedFailure = true;
                         }
                       }});
  SCOPE_EXIT {
    failure::removeHandler(name);
  };
  auto server = TestServer::create();
  TestClient brokenClient(
      "localhost",
      server->getListenPort(),
      200,
      mc_umbrella_protocol_DONOTUSE,
      ssl);
  TestClient client(
      "localhost",
      server->getListenPort(),
      200,
      mc_umbrella_protocol_DONOTUSE,
      validClientSsl());
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
  testCerts("test-invalidCerts", invalidClientSsl(), 1);
}

TEST(AsyncMcClient, brokenCerts) {
  testCerts("test-brokenCerts", brokenClientSsl(), 2);
}

TEST(AsyncMcClient, noCerts) {
  // we expect the no cert case to fail by default since the test server will
  // require peer certs by default
  testCerts("test-nocerts", noCertClientSsl(), 2);
}

TEST(AsyncMcClient, testClientFinalize) {
  folly::AsyncTransportWrapper* transportCalledInFinalizer;
  McSSLUtil::setApplicationClientSSLFinalizer(
      [&transportCalledInFinalizer](folly::AsyncTransportWrapper* transport) {
        transportCalledInFinalizer = transport;
      });

  TestServer::Config config;
  config.outOfOrder = false;
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost",
      server->getListenPort(),
      200,
      mc_caret_protocol,
      validClientSsl());

  client.sendGet("test1", mc_res_found);
  client.waitForReplies();
  server->shutdown();
  server->join();
  EXPECT_EQ(client.getClient().getTransport(), transportCalledInFinalizer);
  EXPECT_EQ(1, server->getAcceptedConns());

  // Unset so we don't pollute other tests
  McSSLUtil::setApplicationClientSSLFinalizer(
      [](folly::AsyncTransportWrapper*) {});
}

void inflightThrottleTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
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
  inflightThrottleTest(folly::none);
}

TEST(AsyncMcClient, inflightThrottleSsl) {
  inflightThrottleTest(validClientSsl());
}

void inflightThrottleFlushTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
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
  inflightThrottleFlushTest(folly::none);
}

TEST(AsyncMcClient, inflightThrottleFlushSsl) {
  inflightThrottleFlushTest(validClientSsl());
}

void outstandingThrottleTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
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
  outstandingThrottleTest(folly::none);
}

TEST(AsyncMcClient, outstandingThrottleSsl) {
  outstandingThrottleTest(validClientSsl());
}

void connectionErrorTest(folly::Optional<SSLTestPaths> ssl) {
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client1(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
  TestClient client2(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
  client1.sendGet("shutdown", mc_res_notfound);
  client1.waitForReplies();
  /* sleep override */ usleep(10000);
  client2.sendGet("test", mc_res_connect_error);
  client2.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, connectionError) {
  connectionErrorTest(folly::none);
}

TEST(AsyncMcClient, connectionErrorSsl) {
  connectionErrorTest(validClientSsl());
}

void basicTest(
    mc_protocol_t protocol,
    folly::Optional<SSLTestPaths> clientSsl,
    uint64_t qosClass = 0,
    uint64_t qosPath = 0,
    TestServer::Config config = TestServer::Config()) {
  config.outOfOrder = (protocol != mc_ascii_protocol);
  config.useSsl = clientSsl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost",
      server->getListenPort(),
      200,
      protocol,
      clientSsl,
      qosClass,
      qosPath);
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
  basicTest(mc_ascii_protocol, folly::none);
}

TEST(AsyncMcClient, basicUmbrella) {
  basicTest(mc_umbrella_protocol_DONOTUSE, folly::none);
}

TEST(AsyncMcClient, basicCaret) {
  basicTest(mc_caret_protocol, folly::none);
}

TEST(AsyncMcClient, basicAsciiSsl) {
  basicTest(mc_ascii_protocol, validClientSsl());
}

TEST(AsyncMcClient, basicUmbrellaSsl) {
  basicTest(mc_umbrella_protocol_DONOTUSE, validClientSsl());
}

TEST(AsyncMcClient, basicCaretSsl) {
  basicTest(mc_caret_protocol, validClientSsl());
}

TEST(AsyncMcClient, basicCaretSslNoCerts) {
  TestServer::Config cfg;
  cfg.requirePeerCerts = false;
  basicTest(mc_caret_protocol, noCertClientSsl(), 0, 0, cfg);
  basicTest(mc_caret_protocol, validClientSsl(), 0, 0, cfg);
}

void qosTest(
    mc_protocol_t protocol,
    folly::Optional<SSLTestPaths> ssl,
    uint64_t qosClass,
    uint64_t qosPath) {
  basicTest(protocol, ssl, qosClass, qosPath);
}

TEST(AsyncMcClient, qosClass1) {
  qosTest(mc_umbrella_protocol_DONOTUSE, folly::none, 1, 0);
}

TEST(AsyncMcClient, qosClass2) {
  qosTest(mc_ascii_protocol, folly::none, 2, 1);
}

TEST(AsyncMcClient, qosClass3) {
  qosTest(mc_umbrella_protocol_DONOTUSE, validClientSsl(), 3, 2);
}

TEST(AsyncMcClient, qosClass4) {
  qosTest(mc_ascii_protocol, validClientSsl(), 4, 3);
}

void reconnectTest(mc_protocol_t protocol) {
  auto bigValue = genBigValue();

  TestServer::Config config;
  config.outOfOrder = (protocol == mc_umbrella_protocol_DONOTUSE);
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));

  TestClient client("localhost", server->getListenPort(), 100, protocol);
  client.sendGet("test1", mc_res_found, 600);
  client.sendSet("test", "testValue", mc_res_stored, 600);
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
  reconnectTest(mc_umbrella_protocol_DONOTUSE);
}

void reconnectImmediatelyTest(mc_protocol_t protocol) {
  auto bigValue = genBigValue();

  TestServer::Config config;
  config.outOfOrder = (protocol == mc_umbrella_protocol_DONOTUSE);
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
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
  reconnectImmediatelyTest(mc_umbrella_protocol_DONOTUSE);
}

void bigKeyTest(mc_protocol_t protocol) {
  TestServer::Config config;
  config.outOfOrder = (protocol == mc_umbrella_protocol_DONOTUSE);
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
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
  auto eventBase = std::make_unique<folly::EventBase>();
  auto fiberManager = std::make_unique<folly::fibers::FiberManager>(
      std::make_unique<folly::fibers::EventBaseLoopController>());
  dynamic_cast<folly::fibers::EventBaseLoopController&>(
      fiberManager->loopController())
      .attachEventBase(*eventBase);
  bool wasUp = false;
  bool replied = false;
  bool wentDown = false;

  ConnectionOptions opts("100::", 11302, mc_ascii_protocol);
  opts.writeTimeout = std::chrono::milliseconds(1000);
  auto client = std::make_unique<AsyncMcClient>(*eventBase, opts);
  client->setStatusCallbacks(
      [&wasUp](const folly::AsyncTransportWrapper&) { wasUp = true; },
      [&wentDown](AsyncMcClient::ConnectionDownReason) { wentDown = true; });

  fiberManager->addTask([&client, &replied] {
    McGetRequest req("hold");
    auto reply = client->sendSync(req, std::chrono::milliseconds(100));
    EXPECT_STREQ(
        mc_res_to_string(reply.result()),
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
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol);
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
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol);
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
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  // Use very large write timeout, so that we never timeout writes.
  TestClient client(
      "localhost", server->getListenPort(), 10000, mc_ascii_protocol);
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
  client.sendGet("flush", mc_res_found, 600);
  client.sendGet("test3", mc_res_found);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcClient, oooUmbrellaTimeouts) {
  TestServer::Config config;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_umbrella_protocol_DONOTUSE);
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
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = false;
  config.maxConns = 3;
  auto server = TestServer::create(std::move(config));

  bool wentDown = false;

  /* Create a client to see if it gets evicted. */
  TestClient client("localhost", server->getListenPort(), 1, mc_ascii_protocol);
  client.setStatusCallbacks(
      [](const folly::AsyncTransportWrapper&) {},
      [&wentDown](AsyncMcClient::ConnectionDownReason) { wentDown = true; });
  client.sendGet("test", mc_res_found);
  client.waitForReplies();

  /* Create 3 more clients to evict the first client. */
  TestClient client2(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol);
  client2.sendGet("test", mc_res_found);
  client2.waitForReplies();
  TestClient client3(
      "localhost", server->getListenPort(), 300, mc_ascii_protocol);
  client3.sendGet("test", mc_res_found);
  client3.waitForReplies();
  TestClient client4(
      "localhost", server->getListenPort(), 400, mc_ascii_protocol);
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

  TestClient client(
      "localhost", sock.getPort(), 200, mc_umbrella_protocol_DONOTUSE);
  client.sendGet("test", expectedResult);
  client.waitForReplies();
  serverThread.join();
}

TEST(AsyncMcClient, binaryUmbrellaReply) {
  // This is a serialized umbrella reply for get operation with
  // mc_res_notfound result and reqid = 1.
  std::string data{
      '}',    '\000', '\000', '\003', '\000', '\000', '\000', ',',    '\000',
      '\001', '\000', '\001', '\000', '\000', '\000', '\000', '\000', '\000',
      '\000', '\005', '\000', '\004', '\000', '\004', '\000', '\000', '\000',
      '\000', '\000', '\000', '\000', '\001', '\000', '\001', '\000', '\002',
      '\000', '\000', '\000', '\000', '\000', '\000', '\000', '\003'};

  umbrellaBinaryReply(data, mc_res_notfound);
}

TEST(AsyncMcClient, curruptedUmbrellaReply) {
  // This is a serialized umbrella reply for get operation with
  // reqid = 1, it contains invalid result code (771).
  std::string data{
      '}',    '\000', '\000', '\003', '\000', '\000', '\000', ',',    '\000',
      '\001', '\000', '\001', '\000', '\000', '\000', '\000', '\000', '\000',
      '\000', '\005', '\000', '\004', '\000', '\004', '\000', '\000', '\000',
      '\000', '\000', '\000', '\000', '\001', '\000', '\001', '\000', '\002',
      '\000', '\000', '\000', '\000', '\000', '\000', '\003', '\003'};

  umbrellaBinaryReply(data, mc_res_remote_error);
}

TEST(AsyncMcClient, SslSessionCache) {
  TestServer::Config config;
  config.useDefaultVersion = true;
  config.numThreads = 4;
  config.useTicketKeySeeds = true;
  auto server = TestServer::create(std::move(config));
  auto constexpr nConnAttempts = 10;

  auto sendAndCheckRequest = [](TestClient& client, int i) {
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
  };

  for (int i = 0; i < nConnAttempts; i++) {
    TestClient client(
        "::1",
        server->getListenPort(),
        200,
        mc_umbrella_protocol_DONOTUSE,
        validClientSsl());
    sendAndCheckRequest(client, i);
  }

  // do the same test w/ service identity
  // we should expect the first attempt to not resume
  for (int i = 0; i < nConnAttempts; i++) {
    TestClient client(
        "::1",
        server->getListenPort(),
        200,
        mc_umbrella_protocol_DONOTUSE,
        validClientSsl(),
        0,
        0,
        "test");
    sendAndCheckRequest(client, i);
  }

  // shutdown the server
  TestClient client(
      "::1",
      server->getListenPort(),
      200,
      mc_umbrella_protocol_DONOTUSE,
      validClientSsl());
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  server->join();
}

void versionTest(mc_protocol_t protocol, bool useDefaultVersion) {
  TestServer::Config config;
  config.outOfOrder = (protocol != mc_ascii_protocol);
  config.useSsl = false;
  config.maxConns = 10;
  config.useDefaultVersion = useDefaultVersion;
  auto server = TestServer::create(std::move(config));
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
  versionTest(mc_umbrella_protocol_DONOTUSE, true);
}

TEST(AsyncMcClient, umbrellaVersionUserSpecified) {
  versionTest(mc_umbrella_protocol_DONOTUSE, false);
}

TEST(AsyncMcClient, caretVersionDefault) {
  versionTest(mc_caret_protocol, true);
}

TEST(AsyncMcClient, caretVersionUserSpecified) {
  versionTest(mc_caret_protocol, false);
}

TEST(AsyncMcClient, caretAdditionalFields) {
  TestServer::Config config;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol);
  client.sendGet("trace_id", mc_res_found);
  client.waitForReplies();
  server->shutdown();
  server->join();
}

TEST(AsyncMcClient, caretGoAway) {
  TestServer::Config config;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol);
  client.sendGet("test", mc_res_found);
  client.sendGet("hold", mc_res_found);
  client.setStatusCallbacks(
      [](const folly::AsyncTransportWrapper&) {},
      [&client](AsyncMcClient::ConnectionDownReason reason) {
        if (reason == AsyncMcClient::ConnectionDownReason::SERVER_GONE_AWAY) {
          LOG(INFO) << "Server gone away, flushing";
          client.sendGet("flush", mc_res_found);
        }
      });
  client.waitForReplies(1);
  server->shutdown();
  client.waitForReplies();
  server->join();
}

TEST(AsyncMcClient, contextProviders) {
  auto clientCtxPaths = validClientSsl();
  auto serverCtxPaths = validSsl();

  auto clientCtx1 = getClientContext(
      clientCtxPaths.sslCertPath,
      clientCtxPaths.sslKeyPath,
      clientCtxPaths.sslCaPath);
  auto clientCtx2 = getClientContext(
      clientCtxPaths.sslCertPath,
      clientCtxPaths.sslKeyPath,
      clientCtxPaths.sslCaPath);

  auto serverCtx1 = getServerContexts(
                        serverCtxPaths.sslCertPath,
                        serverCtxPaths.sslKeyPath,
                        serverCtxPaths.sslCaPath,
                        true,
                        folly::none)
                        .first;
  auto serverCtx2 = getServerContexts(
                        serverCtxPaths.sslCertPath,
                        serverCtxPaths.sslKeyPath,
                        serverCtxPaths.sslCaPath,
                        true,
                        folly::none)
                        .first;

  // client contexts should be the same since they are
  // thread local cached
  EXPECT_EQ(clientCtx1, clientCtx2);

  // server contexts should be the same for the same reason
  EXPECT_EQ(serverCtx1, serverCtx2);

  EXPECT_NE(clientCtx1, serverCtx1);
}

using TFOTestParams = std::tuple<bool, bool, bool>;

class AsyncMcClientTFOTest : public testing::TestWithParam<TFOTestParams> {};

TEST_P(AsyncMcClientTFOTest, testTfoWithSSL) {
  auto serverEnabled = std::get<0>(GetParam());
  auto clientEnabled = std::get<1>(GetParam());

  TestServer::Config config;
  config.useDefaultVersion = true;
  config.numThreads = 4;
  config.useTicketKeySeeds = true;
  config.tfoEnabled = serverEnabled;
  auto server = TestServer::create(std::move(config));

  auto offloadHandshake = std::get<2>(GetParam());
  auto constexpr nConnAttempts = 10;

  auto sendReq = [serverEnabled, clientEnabled](TestClient& client) {
    client.sendGet("test", mc_res_found);
    client.waitForReplies();
    auto transport = client.getClient().getTransport();
    auto* socket = transport->getUnderlyingTransport<folly::AsyncSSLSocket>();
    if (clientEnabled) {
      EXPECT_TRUE(socket->getTFOAttempted());
      EXPECT_TRUE(socket->getTFOFinished());
      // we can not guarantee socket->getTFOSucceeded() will return true
      // unless there are specific kernel + host settings applied
      if (!serverEnabled) {
        EXPECT_FALSE(socket->getTFOSucceded());
      }
    } else {
      EXPECT_FALSE(socket->getTFOAttempted());
    }
  };

  for (int i = 0; i < nConnAttempts; i++) {
    TestClient client(
        "::1",
        server->getListenPort(),
        200,
        mc_caret_protocol,
        validClientSsl(),
        0,
        0,
        "",
        nullptr,
        clientEnabled,
        offloadHandshake);
    sendReq(client);
  }

  // shutdown the server
  TestClient client(
      "::1", server->getListenPort(), 200, mc_caret_protocol, validClientSsl());
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  server->join();
}

INSTANTIATE_TEST_CASE_P(
    AsyncMcClientTest,
    AsyncMcClientTFOTest,
    testing::Combine(testing::Bool(), testing::Bool(), testing::Bool()));

class AsyncMcClientSSLOffloadTest : public testing::TestWithParam<bool> {
 public:
  void TearDown() override {
    McSSLUtil::setApplicationSSLVerifier(nullptr);
  }

 protected:
  void enableSSL(ConnectionOptions& opts) {
    auto paths = validClientSsl();
    opts.securityMech = SecurityMech::TLS;
    opts.sslPemCertPath = paths.sslCertPath;
    opts.sslPemKeyPath = paths.sslKeyPath;
    opts.sslPemCaPath = paths.sslCaPath;
  }

  std::unique_ptr<TestServer> createServer() {
    TestServer::Config cfg;
    cfg.outOfOrder = false;
    cfg.useSsl = true;
    return TestServer::create(std::move(cfg));
  }
};

TEST_P(AsyncMcClientSSLOffloadTest, connectErrors) {
  bool verifyCalled = false;
  McSSLUtil::setApplicationSSLVerifier(
      [&](folly::AsyncSSLSocket*, bool, X509_STORE_CTX*) {
        verifyCalled = true;
        return false;
      });
  auto server = createServer();

  TestClient sadClient(
      "::1",
      server->getListenPort(),
      200,
      mc_caret_protocol,
      validClientSsl(),
      0,
      0,
      "",
      nullptr,
      false,
      GetParam());
  sadClient.sendGet("empty", mc_res_connect_error);
  sadClient.waitForReplies();

  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
  EXPECT_TRUE(verifyCalled);
}

TEST_P(AsyncMcClientSSLOffloadTest, closeNow) {
  auto server = createServer();
  folly::EventBase evb;
  ConnectionOptions opts("::1", server->getListenPort(), mc_caret_protocol);
  opts.writeTimeout = std::chrono::milliseconds(1000);
  enableSSL(opts);
  opts.sslHandshakeOffload = GetParam();
  auto lc = std::make_unique<folly::fibers::EventBaseLoopController>();
  lc->attachEventBase(evb);
  folly::fibers::FiberManager fm(std::move(lc));
  bool upCalled = false;
  folly::Optional<AsyncMcClient::ConnectionDownReason> downReason;
  auto upFunc = [&](const folly::AsyncTransportWrapper&) { upCalled = true; };
  auto downFunc = [&](AsyncMcClient::ConnectionDownReason reason) {
    downReason = reason;
  };

  auto client = std::make_unique<AsyncMcClient>(evb, opts);
  client->setStatusCallbacks(upFunc, downFunc);
  auto clientPtr = client.get();
  fm.addTask([clientPtr] {
    McGetRequest req("test");
    clientPtr->sendSync(req, std::chrono::milliseconds(200), nullptr);
  });
  evb.loopOnce();
  client->closeNow();
  evb.loop();
  EXPECT_FALSE(upCalled);
  EXPECT_TRUE(downReason.hasValue());
  EXPECT_EQ(*downReason, AsyncMcClient::ConnectionDownReason::ABORTED);
}

TEST_P(AsyncMcClientSSLOffloadTest, clientReset) {
  auto server = createServer();
  folly::EventBase evb;
  ConnectionOptions opts("::1", server->getListenPort(), mc_caret_protocol);
  opts.writeTimeout = std::chrono::milliseconds(1000);
  enableSSL(opts);
  opts.sslHandshakeOffload = GetParam();
  auto lc = std::make_unique<folly::fibers::EventBaseLoopController>();
  lc->attachEventBase(evb);
  folly::fibers::FiberManager fm(std::move(lc));
  folly::Optional<AsyncMcClient::ConnectionDownReason> downReason;
  auto client = std::make_unique<AsyncMcClient>(evb, opts);
  auto clientPtr = client.get();
  fm.addTask([clientPtr] {
    McGetRequest req("test");
    clientPtr->sendSync(req, std::chrono::milliseconds(200), nullptr);
  });
  evb.loopOnce();
  client.reset();
  evb.loop();
}

INSTANTIATE_TEST_CASE_P(
    AsyncMcClientTest,
    AsyncMcClientSSLOffloadTest,
    testing::Bool());
