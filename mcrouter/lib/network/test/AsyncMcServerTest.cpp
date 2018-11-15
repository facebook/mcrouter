/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <gtest/gtest.h>

#include <folly/io/async/ssl/OpenSSLUtils.h>

#include "mcrouter/lib/network/McSSLUtil.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::test;

struct VerifyCommonNameOnRequest {
  VerifyCommonNameOnRequest(folly::fibers::Baton&, bool) {}

  template <class Request>
  void onRequest(McServerRequestContext&& ctx, Request&&) {
    constexpr folly::StringPiece expectedCommonName{"Asox Company"};
    auto& session = ctx.session();
    auto clientCommonName = session.getClientCommonName();
    LOG(INFO) << "Client CN = " << clientCommonName.toString();
    EXPECT_EQ(session.getClientCommonName(), expectedCommonName);
    if (clientCommonName == expectedCommonName) {
      McServerRequestContext::reply(
          std::move(ctx), ReplyT<Request>(mc_res_found));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), ReplyT<Request>(mc_res_client_error));
    }
  }
};

TEST(AsyncMcServer, basic) {
  TestServer::Config config;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));

  LOG(INFO) << "creating client";

  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol);
  client.sendGet("empty", mc_res_found);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcServer, sslCertCommonName) {
  TestServer::Config config;
  config.outOfOrder = false;
  auto server = TestServer::create(std::move(config));

  LOG(INFO) << "creating client";

  TestClient clientWithSsl(
      "localhost",
      server->getListenPort(),
      200,
      mc_ascii_protocol,
      validClientSsl());
  clientWithSsl.sendGet("empty", mc_res_found);
  clientWithSsl.waitForReplies();

  TestClient clientWithoutSsl(
      "localhost",
      server->getListenPort(),
      200,
      mc_ascii_protocol,
      folly::none);
  clientWithoutSsl.sendGet("empty", mc_res_remote_error);
  clientWithoutSsl.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcServer, sslVerify) {
  McSSLUtil::setApplicationSSLVerifier(
      [](folly::AsyncSSLSocket*, bool, X509_STORE_CTX*) { return false; });
  TestServer::Config config;
  config.outOfOrder = false;
  auto server = TestServer::create(std::move(config));

  TestClient sadClient(
      "localhost",
      server->getListenPort(),
      200,
      mc_ascii_protocol,
      validClientSsl());
  sadClient.sendGet("empty", mc_res_connect_error);
  sadClient.waitForReplies();

  server->shutdown();
  server->join();
  EXPECT_EQ(0, server->getAcceptedConns());
}

TEST(AsyncMcServer, rejectAllConnections) {
  TestServer::Config config;
  config.useSsl = false;
  config.onConnectionAcceptedAdditionalCb = [](McServerSession& session) {
    session.close();
  };
  auto server = TestServer::create(std::move(config));

  LOG(INFO) << "creating client";

  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol);
  client.sendGet("empty", mc_res_remote_error);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcServer, tcpZeroCopyDisabled) {
  auto bigValue = genBigValue();
  TestServer::Config config;
  config.outOfOrder = false;
  config.tcpZeroCopyThresholdBytes = 0;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  // Use very large write timeout, so that we never timeout writes.
  TestClient client(
      "localhost", server->getListenPort(), 10000, mc_caret_protocol);
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

TEST(AsyncMcServer, tcpZeroCopyEnabled) {
  auto bigValue = genBigValue();
  TestServer::Config config;
  config.outOfOrder = false;
  config.tcpZeroCopyThresholdBytes = 12000;
  config.useSsl = false;
  auto server = TestServer::create(std::move(config));
  // Use very large write timeout, so that we never timeout writes.
  TestClient client(
      "localhost", server->getListenPort(), 10000, mc_caret_protocol);
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

TEST(AsyncMcServer, tcpZeroCopySSLEnabled) {
  McSSLUtil::setApplicationSSLVerifier(
      [](folly::AsyncSSLSocket*, bool, X509_STORE_CTX*) { return false; });
  TestServer::Config config;
  config.tcpZeroCopyThresholdBytes = 12000;
  config.outOfOrder = false;
  auto server = TestServer::create(std::move(config));

  TestClient sadClient(
      "localhost",
      server->getListenPort(),
      200,
      mc_ascii_protocol,
      validClientSsl());
  sadClient.sendGet("empty", mc_res_connect_error);
  sadClient.waitForReplies();
}

TEST(AsyncMcServer, onAccepted_PlainText) {
  TestServer::Config config;
  config.useSsl = false;
  bool onAcceptedCalled = false;
  config.onConnectionAcceptedAdditionalCb = [&](McServerSession& session) {
    onAcceptedCalled = true;
    EXPECT_EQ(SecurityMech::NONE, session.securityMech());
  };
  auto server = TestServer::create(std::move(config));

  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol);
  client.sendGet("empty", mc_res_found);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
  EXPECT_TRUE(onAcceptedCalled);
}

TEST(AsyncMcServer, onAccepted_Tls) {
  TestServer::Config config;
  bool onAcceptedCalled = false;
  config.onConnectionAcceptedAdditionalCb = [&](McServerSession& session) {
    onAcceptedCalled = true;
    EXPECT_EQ(SecurityMech::TLS, session.securityMech());
  };
  auto server = TestServer::create(std::move(config));

  TestClient client(
      "localhost",
      server->getListenPort(),
      200,
      mc_caret_protocol,
      validClientSsl());
  client.sendGet("empty", mc_res_found);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
  EXPECT_TRUE(onAcceptedCalled);
}

TEST(AsyncMcServer, onAccepted_Tls13Fizz) {
  TestServer::Config config;
  bool onAcceptedCalled = false;
  config.onConnectionAcceptedAdditionalCb = [&](McServerSession& session) {
    onAcceptedCalled = true;
    EXPECT_EQ(SecurityMech::TLS13_FIZZ, session.securityMech());
  };
  auto server = TestServer::create(std::move(config));

  auto sslOpts = validClientSsl();
  sslOpts.mech = SecurityMech::TLS13_FIZZ;

  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  client.sendGet("empty", mc_res_found);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
  EXPECT_TRUE(onAcceptedCalled);
}

TEST(AsyncMcServer, onAccepted_TlsToPlainText) {
  TestServer::Config config;
  bool onAcceptedCalled = false;
  config.onConnectionAcceptedAdditionalCb = [&](McServerSession& session) {
    onAcceptedCalled = true;
    EXPECT_EQ(SecurityMech::TLS_TO_PLAINTEXT, session.securityMech());
  };
  auto server = TestServer::create(std::move(config));

  auto sslOpts = validClientSsl();
  sslOpts.mech = SecurityMech::TLS_TO_PLAINTEXT;

  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  client.sendGet("empty", mc_res_found);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
  EXPECT_TRUE(onAcceptedCalled);
}

TEST(AsyncMcServer, onAccepted_RejectTlsToPlainText) {
  TestServer::Config config;
  config.onConnectionAcceptedAdditionalCb = [](McServerSession& session) {
    // we don't like TLS_TO_PLAINTEXT connections!
    if (session.securityMech() == SecurityMech::TLS_TO_PLAINTEXT) {
      session.close();
    }
  };
  auto server = TestServer::create(std::move(config));

  auto sslOpts = validClientSsl();

  // tls-to-plaintext should be rejected.
  sslOpts.mech = SecurityMech::TLS_TO_PLAINTEXT;
  TestClient badClient(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  badClient.sendGet("empty", mc_res_remote_error);
  badClient.waitForReplies();

  // tls is fine.
  sslOpts.mech = SecurityMech::TLS;
  TestClient goodClient(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  goodClient.sendGet("empty", mc_res_found);
  goodClient.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(2, server->getAcceptedConns());
}

TEST(AsyncMcServer, onAccepted_RejectSecure) {
  TestServer::Config config;
  config.onConnectionAcceptedAdditionalCb = [](McServerSession& session) {
    // we don't like connections at all!
    session.close();
  };
  auto server = TestServer::create(std::move(config));

  auto sslOpts = validClientSsl();

  sslOpts.mech = SecurityMech::TLS_TO_PLAINTEXT;
  TestClient client1(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  client1.sendGet("empty", mc_res_remote_error);
  client1.waitForReplies();

  sslOpts.mech = SecurityMech::TLS;
  TestClient client2(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  client2.sendGet("empty", mc_res_remote_error);
  client2.waitForReplies();

  sslOpts.mech = SecurityMech::TLS13_FIZZ;
  TestClient client3(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  client3.sendGet("empty", mc_res_remote_error);
  client3.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(3, server->getAcceptedConns());
}

TEST(AsyncMcServer, onAccepted_RejectPlainText) {
  TestServer::Config config;
  config.useSsl = false;
  config.onConnectionAcceptedAdditionalCb = [](McServerSession& session) {
    // we don't like connections at all!
    session.close();
  };
  auto server = TestServer::create(std::move(config));

  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol);
  client.sendGet("empty", mc_res_remote_error);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcServer, onAccepted_RejectBasedOnCert) {
  TestServer::Config config;
  config.requirePeerCerts = false;
  config.onConnectionAcceptedAdditionalCb = [](McServerSession& session) {
    EXPECT_EQ(SecurityMech::TLS_TO_PLAINTEXT, session.securityMech());
    auto peerCert = session.getTransport()->getPeerCert();
    EXPECT_TRUE(peerCert);

    // we just trust certs from the "The Cool Company"
    auto certName = folly::ssl::OpenSSLUtils::getCommonName(peerCert.get());
    LOG(INFO) << "Cert common name: " << certName;
    if (!folly::StringPiece(certName).contains("The Cool Company")) {
      session.close();
    }
  };
  auto server = TestServer::create(std::move(config));

  auto sslOpts = validClientSsl();
  sslOpts.mech = SecurityMech::TLS_TO_PLAINTEXT;
  TestClient client(
      "localhost", server->getListenPort(), 200, mc_caret_protocol, sslOpts);
  client.sendGet("empty", mc_res_remote_error);
  client.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(1, server->getAcceptedConns());
}

TEST(AsyncMcServer, onAccepted_RejectEmptyCert) {
  TestServer::Config config;
  config.requirePeerCerts = false;
  config.onConnectionAcceptedAdditionalCb = [](McServerSession& session) {
    auto peerCert = session.getTransport()->getPeerCert();
    if (!peerCert) {
      // we don't like missing certs.
      session.close();
    }
  };
  auto server = TestServer::create(std::move(config));

  TestClient client(
      "localhost",
      server->getListenPort(),
      200,
      mc_caret_protocol,
      noCertClientSsl());
  client.sendGet("empty", mc_res_remote_error);
  client.waitForReplies();

  TestClient goodClient(
      "localhost",
      server->getListenPort(),
      200,
      mc_caret_protocol,
      validClientSsl(),
      0 /* qosClass */,
      0 /* qosPath */,
      "" /* serviceIdentity */,
      nullptr /* compressionMap */,
      false /* enableTfo */,
      false /* offloadHandshakes */,
      false /* sessionCachingEnabled */);
  goodClient.sendGet("empty", mc_res_found);
  goodClient.waitForReplies();

  auto sslOpts = validClientSsl();
  sslOpts.mech = SecurityMech::TLS_TO_PLAINTEXT;
  TestClient goodClient2(
      "localhost",
      server->getListenPort(),
      200,
      mc_caret_protocol,
      sslOpts,
      0 /* qosClass */,
      0 /* qosPath */,
      "" /* serviceIdentity */,
      nullptr /* compressionMap */,
      false /* enableTfo */,
      false /* offloadHandshakes */,
      false /* sessionCachingEnabled */);
  goodClient2.sendGet("empty", mc_res_found);
  goodClient2.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(3, server->getAcceptedConns());
}
