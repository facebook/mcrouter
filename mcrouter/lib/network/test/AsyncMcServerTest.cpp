/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/network/test/TestClientServerUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::test;

struct VeryifyCommonNameOnRequest {
  VeryifyCommonNameOnRequest(std::atomic<bool>&, bool) {}

  template <class Request>
  void onRequest(McServerRequestContext&& ctx, Request&&) {
    constexpr folly::StringPiece expectedCommonName{"Asox Company"};
    auto& session = ctx.session();
    auto clientCommonName = session.getClientCommonName();
    LOG(INFO) << "Client CN = " << clientCommonName.toString();
    EXPECT_EQ(session.getClientCommonName(), expectedCommonName);
    if (clientCommonName == expectedCommonName) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_found));
    } else {
      McServerRequestContext::reply(std::move(ctx),
                                    McReply(mc_res_client_error));
    }
  }
};

TEST(AsyncMcServer, sslCertCommonName) {
  auto server =
      TestServer::create<VeryifyCommonNameOnRequest>(false, true /* use ssl */);

  LOG(INFO) << "creating client";

  TestClient clientWithSsl(
      "localhost", server->getListenPort(), 200, mc_ascii_protocol, validSsl());
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
