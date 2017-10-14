/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/network/McServerSSLUtil.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::test;

struct VeryifyCommonNameOnRequest {
  VeryifyCommonNameOnRequest(folly::fibers::Baton&, bool) {}

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

TEST(AsyncMcServer, sslCertCommonName) {
  auto server =
      TestServer::create<VeryifyCommonNameOnRequest>(false, true /* use ssl */);

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
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, noSsl());
  clientWithoutSsl.sendGet("empty", mc_res_remote_error);
  clientWithoutSsl.waitForReplies();

  LOG(INFO) << "Joining...";
  server->shutdown();
  server->join();
  EXPECT_EQ(2, server->getAcceptedConns());
}

TEST(AsyncMcServer, sslVerify) {
  McServerSSLUtil::setApplicationSSLVerifier(
      [](folly::AsyncSSLSocket*, bool, X509_STORE_CTX*) { return false; });
  auto server = TestServer::create(false, true /* use ssl */);

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
