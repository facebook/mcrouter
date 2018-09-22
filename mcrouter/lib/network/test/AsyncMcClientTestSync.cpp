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
#include "mcrouter/lib/network/SecurityOptions.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/test/ListenSocket.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::test;
using namespace testing;

namespace folly {
class AsyncSocket;
} // namespace folly

folly::Optional<SSLTestPaths> getTlsToPtSSL() {
  auto res = validClientSsl();
  res.mech = SecurityMech::TLS_TO_PLAINTEXT;
  return res;
}

class AsyncMcClientSimpleTest
    : public TestWithParam<folly::Optional<SSLTestPaths>> {
 public:
  virtual ~AsyncMcClientSimpleTest() = default;
};

TEST_P(AsyncMcClientSimpleTest, serverShutdownTest) {
  auto ssl = GetParam();
  TestServer::Config config;
  config.outOfOrder = false;
  config.useSsl = ssl.hasValue();
  auto server = TestServer::create(std::move(config));
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, ssl);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  // ensure transport matches the mech
  auto transport = client.getClient().getTransport();
  if (!ssl.hasValue()) {
    EXPECT_EQ(transport->getSecurityProtocol(), "");
  } else if (ssl->mech == SecurityMech::TLS) {
    EXPECT_EQ(transport->getSecurityProtocol(), "TLS");
  } else {
    // TLS_TO_PLAINTEXT
    EXPECT_EQ(transport->getSecurityProtocol(), "mc_plaintext");
    auto* sslsock = transport->getUnderlyingTransport<folly::AsyncSSLSocket>();
    EXPECT_EQ(sslsock, nullptr);
    auto peerCert = transport->getPeerCertificate();
    auto selfCert = transport->getSelfCertificate();
    EXPECT_NE(peerCert, nullptr);
    EXPECT_NE(selfCert, nullptr);
  }

  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST_P(AsyncMcClientSimpleTest, asciiTimeout) {
  auto ssl = GetParam();
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

TEST_P(AsyncMcClientSimpleTest, umbrellaTimeout) {
  auto ssl = GetParam();
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

TEST_P(AsyncMcClientSimpleTest, noServerTimeout) {
  auto ssl = GetParam();
  TestClient client("100::", 11302, 200, mc_ascii_protocol, ssl);
  client.sendGet("hold", mc_res_connect_timeout);
  client.waitForReplies();
}

TEST_P(AsyncMcClientSimpleTest, immediateConnectFail) {
  auto ssl = GetParam();
  TestClient client("255.255.255.255", 12345, 200, mc_ascii_protocol, ssl);
  client.sendGet("nohold", mc_res_connect_error);
  client.waitForReplies();
}

TEST_P(AsyncMcClientSimpleTest, inflightThrottle) {
  auto ssl = GetParam();
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

TEST_P(AsyncMcClientSimpleTest, inflightThrottleFlush) {
  auto ssl = GetParam();
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

TEST_P(AsyncMcClientSimpleTest, outstandingThrottle) {
  auto ssl = GetParam();
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

TEST_P(AsyncMcClientSimpleTest, connectionError) {
  auto ssl = GetParam();
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

INSTANTIATE_TEST_CASE_P(
    AsyncMcClientTest,
    AsyncMcClientSimpleTest,
    Values(folly::none, validClientSsl(), getTlsToPtSSL()));

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

class AsyncMcClientBasicTestBase {
 protected:
  void basicTest(
      mc_protocol_t protocol,
      folly::Optional<SSLTestPaths> clientSsl,
      uint64_t qosClass,
      uint64_t qosPath) {
    config.outOfOrder = (protocol != mc_ascii_protocol);
    config.useSsl = clientSsl.hasValue();
    auto server = TestServer::create(config);
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

  TestServer::Config config;
};

using ProtocolAndSSL = std::tuple<mc_protocol_t, folly::Optional<SSLTestPaths>>;

class AsyncMcClientBasicTest : public AsyncMcClientBasicTestBase,
                               public TestWithParam<ProtocolAndSSL> {
 protected:
  void runTest(uint64_t qosClass = 0, uint64_t qosPath = 0) {
    auto protocol = std::get<0>(GetParam());
    auto ssl = std::get<1>(GetParam());
    basicTest(protocol, ssl, qosClass, qosPath);
  }
};

TEST_P(AsyncMcClientBasicTest, basicTest) {
  runTest();
}

TEST_P(AsyncMcClientBasicTest, qosClass) {
  for (uint64_t path = 0; path < 4; ++path) {
    LOG(INFO) << "Path: " << path;
    // SCOPED_TRACE(folly::to<std::string>("Path=", path));
    auto qosClass = path + 1;
    runTest(qosClass, path);
  }
}

INSTANTIATE_TEST_CASE_P(
    AsyncMcClientTest,
    AsyncMcClientBasicTest,
    Combine(
        Values(
            mc_ascii_protocol,
            mc_umbrella_protocol_DONOTUSE,
            mc_caret_protocol),
        Values(folly::none, validClientSsl(), getTlsToPtSSL())));

TEST_F(AsyncMcClientBasicTest, caretSslNoCerts) {
  config.requirePeerCerts = false;
  basicTest(mc_caret_protocol, noCertClientSsl(), 0, 0);
  basicTest(mc_caret_protocol, validClientSsl(), 0, 0);
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
  opts.connectTimeout = std::chrono::milliseconds(1000);
  auto client = std::make_unique<AsyncMcClient>(*eventBase, opts);
  client->setStatusCallbacks(
      [&wasUp](const folly::AsyncTransportWrapper&, int64_t) { wasUp = true; },
      [&wentDown](AsyncMcClient::ConnectionDownReason, int64_t) {
        wentDown = true;
      });

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
      [](const folly::AsyncTransportWrapper&, int64_t) {},
      [&wentDown](AsyncMcClient::ConnectionDownReason, int64_t) {
        wentDown = true;
      });
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

class AsyncMcClientSessionTest : public TestWithParam<SecurityMech> {};

TEST_P(AsyncMcClientSessionTest, SessionResumption) {
  auto mech = GetParam();
  TestServer::Config config;
  config.useDefaultVersion = true;
  config.numThreads = 4;
  config.useTicketKeySeeds = true;
  auto server = TestServer::create(std::move(config));
  auto constexpr nConnAttempts = 10;

  auto sendAndCheckRequest = [](TestClient& client, int i) {
    LOG(INFO) << "Connection attempt: " << i;
    client.setStatusCallbacks(
        [&](const folly::AsyncTransportWrapper& sock, int64_t) {
          auto* socket = sock.getUnderlyingTransport<folly::AsyncSSLSocket>();
          if (i != 0) {
            EXPECT_TRUE(socket->getSSLSessionReused());
          } else {
            EXPECT_FALSE(socket->getSSLSessionReused());
          }
        },
        nullptr);
    client.sendGet("test", mc_res_found);
    client.waitForReplies();
  };

  auto ssl = validClientSsl();
  ssl.mech = mech;

  for (int i = 0; i < nConnAttempts; i++) {
    TestClient client(
        "::1",
        server->getListenPort(),
        200,
        mc_umbrella_protocol_DONOTUSE,
        ssl);
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
        ssl,
        0,
        0,
        "test");
    sendAndCheckRequest(client, i);
  }

  // shutdown the server
  TestClient client(
      "::1", server->getListenPort(), 200, mc_umbrella_protocol_DONOTUSE, ssl);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  server->join();
}

INSTANTIATE_TEST_CASE_P(
    AsyncMcClientTest,
    AsyncMcClientSessionTest,
    Values(SecurityMech::TLS, SecurityMech::TLS_TO_PLAINTEXT));

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
      [](const folly::AsyncTransportWrapper&, int64_t) {},
      [&client](AsyncMcClient::ConnectionDownReason reason, int64_t) {
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

  SecurityOptions opts;
  opts.sslPemCertPath = clientCtxPaths.sslCertPath;
  opts.sslPemKeyPath = clientCtxPaths.sslKeyPath;
  opts.sslPemCaPath = clientCtxPaths.sslCaPath;
  auto mech = SecurityMech::TLS;
  auto clientCtx1 = getClientContext(opts, mech);
  auto clientCtx2 = getClientContext(opts, mech);

  // make sure mech changes the context
  mech = SecurityMech::TLS_TO_PLAINTEXT;
  auto clientCtx3 = getClientContext(opts, mech);
  auto clientCtx4 = getClientContext(opts, mech);

  auto serverCtxs1 = getServerContexts(
      serverCtxPaths.sslCertPath,
      serverCtxPaths.sslKeyPath,
      serverCtxPaths.sslCaPath,
      true,
      folly::none);
  auto serverCtxs2 = getServerContexts(
      serverCtxPaths.sslCertPath,
      serverCtxPaths.sslKeyPath,
      serverCtxPaths.sslCaPath,
      true,
      folly::none);

  // client contexts should be the same since they are
  // thread local cached
  EXPECT_EQ(clientCtx1, clientCtx2);
  EXPECT_EQ(clientCtx3, clientCtx4);
  EXPECT_NE(clientCtx1, clientCtx3);

  // server contexts should be the same for the same reason
  EXPECT_EQ(serverCtxs1, serverCtxs2);

  // client contexts should not equal server contexts
  EXPECT_NE(clientCtx1, serverCtxs1.first);
  EXPECT_NE(clientCtx3, serverCtxs1.first);
}

using TFOTestParams = std::tuple<bool, bool, bool, SecurityMech>;

class AsyncMcClientTFOTest : public TestWithParam<TFOTestParams> {};

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
    client.setStatusCallbacks(
        [&](const folly::AsyncTransportWrapper& sock, int64_t) {
          auto* socket = sock.getUnderlyingTransport<folly::AsyncSocket>();
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
        },
        nullptr);
    client.sendGet("test", mc_res_found);
    client.waitForReplies();
  };

  auto ssl = validClientSsl();
  ssl.mech = std::get<3>(GetParam());
  for (int i = 0; i < nConnAttempts; i++) {
    TestClient client(
        "::1",
        server->getListenPort(),
        200,
        mc_caret_protocol,
        ssl,
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
      "::1", server->getListenPort(), 200, mc_caret_protocol, ssl);
  client.sendGet("shutdown", mc_res_notfound);
  client.waitForReplies();

  server->join();
}

INSTANTIATE_TEST_CASE_P(
    AsyncMcClientTest,
    AsyncMcClientTFOTest,
    Combine(
        Bool(),
        Bool(),
        Bool(),
        Values(SecurityMech::TLS, SecurityMech::TLS_TO_PLAINTEXT)));

class AsyncMcClientSSLOffloadTest : public TestWithParam<bool> {
 public:
  void TearDown() override {
    McSSLUtil::setApplicationSSLVerifier(nullptr);
  }

 protected:
  void enableSSL(ConnectionOptions& opts) {
    auto paths = validClientSsl();
    auto sslAp = std::make_shared<AccessPoint>(
        opts.accessPoint->getHost(),
        opts.accessPoint->getPort(),
        opts.accessPoint->getProtocol(),
        SecurityMech::TLS,
        opts.accessPoint->compressed(),
        opts.accessPoint->isUnixDomainSocket());
    opts.accessPoint = std::move(sslAp);
    opts.securityOpts.sslPemCertPath = paths.sslCertPath;
    opts.securityOpts.sslPemKeyPath = paths.sslKeyPath;
    opts.securityOpts.sslPemCaPath = paths.sslCaPath;
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
  opts.securityOpts.sslHandshakeOffload = GetParam();
  auto lc = std::make_unique<folly::fibers::EventBaseLoopController>();
  lc->attachEventBase(evb);
  folly::fibers::FiberManager fm(std::move(lc));
  bool upCalled = false;
  folly::Optional<AsyncMcClient::ConnectionDownReason> downReason;
  auto upFunc = [&](const folly::AsyncTransportWrapper&, int64_t) {
    upCalled = true;
  };
  auto downFunc = [&](AsyncMcClient::ConnectionDownReason reason, int64_t) {
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
  opts.securityOpts.sslHandshakeOffload = GetParam();
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

INSTANTIATE_TEST_CASE_P(AsyncMcClientTest, AsyncMcClientSSLOffloadTest, Bool());
