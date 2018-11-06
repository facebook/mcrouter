/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <gtest/gtest.h>

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
  EXPECT_EQ(2, server->getAcceptedConns());
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
  EXPECT_EQ(1, server->getAcceptedConns());
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
