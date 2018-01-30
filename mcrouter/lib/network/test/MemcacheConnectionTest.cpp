/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>
#include <vector>

#include <folly/Conv.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/synchronization/Baton.h>

#include <mcrouter/lib/network/AsyncMcClient.h>
#include <mcrouter/options.h>
#include "mcrouter/lib/network/gen/MemcacheConnection.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"

TEST(MemcacheExternalConnectionTest, simpleExternalConnection) {
  auto server = facebook::memcache::test::TestServer::create(
      false /* outOfOrder */, false /* useSsl */);
  auto conn = std::make_unique<facebook::memcache::MemcacheExternalConnection>(
      facebook::memcache::ConnectionOptions(
          "localhost", server->getListenPort(), mc_caret_protocol));
  facebook::memcache::McSetRequest request("hello");
  request.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "world");
  folly::fibers::Baton baton;
  conn->sendRequestOne(
      request,
      [&baton](
          const facebook::memcache::McSetRequest& /* req */,
          facebook::memcache::McSetReply&& reply) {
        EXPECT_EQ(mc_res_stored, reply.result());
        baton.post();
      });
  baton.wait();
  baton.reset();
  facebook::memcache::McGetRequest getReq("hello");
  conn->sendRequestOne(
      getReq,
      [&baton](
          const facebook::memcache::McGetRequest& /* req */,
          facebook::memcache::McGetReply&& reply) {
        EXPECT_EQ(mc_res_found, reply.result());
        EXPECT_EQ("hello", folly::StringPiece(reply.value()->coalesce()));
        baton.post();
      });
  baton.wait();
  conn.reset();
  server->shutdown();
  server->join();
}

TEST(MemcachePooledConnectionTest, PooledExternalConnection) {
  auto server = facebook::memcache::test::TestServer::create(
      false /* outOfOrder */, false /* useSsl */);
  std::vector<std::unique_ptr<facebook::memcache::MemcacheConnection>> conns;
  for (int i = 0; i < 4; i++) {
    conns.push_back(
        std::make_unique<facebook::memcache::MemcacheExternalConnection>(
            facebook::memcache::ConnectionOptions(
                "localhost", server->getListenPort(), mc_caret_protocol)));
  }
  auto pooledConn =
      std::make_unique<facebook::memcache::MemcachePooledConnection>(
          std::move(conns));
  facebook::memcache::McSetRequest request("pooled");
  request.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "connection");
  folly::fibers::Baton baton;
  pooledConn->sendRequestOne(
      request,
      [&baton](
          const facebook::memcache::McSetRequest& /* req */,
          facebook::memcache::McSetReply&& reply) {
        EXPECT_EQ(mc_res_stored, reply.result());
        baton.post();
      });
  baton.wait();
  baton.reset();
  facebook::memcache::McGetRequest getReq("pooled");
  pooledConn->sendRequestOne(
      getReq,
      [&baton](
          const facebook::memcache::McGetRequest& /* req */,
          facebook::memcache::McGetReply&& reply) {
        EXPECT_EQ(mc_res_found, reply.result());
        EXPECT_EQ("pooled", folly::StringPiece(reply.value()->coalesce()));
        baton.post();
      });
  baton.wait();
  pooledConn.reset();
  server->shutdown();
  server->join();
}

TEST(MemcacheInternalConnectionTest, simpleInternalConnection) {
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  auto server = facebook::memcache::test::TestServer::create(
      false /* outOfOrder */, false /* useSsl */);
  facebook::memcache::McrouterOptions mcrouterOptions;
  mcrouterOptions.num_proxies = 1;
  mcrouterOptions.default_route = "/oregon/*/";
  mcrouterOptions.config_str = folly::sformat(
      R"(
        {{
          "pools": {{
            "A": {{
              "servers": [ "{}:{}" ],
              "protocol": "caret"
            }}
          }},
          "route": "Pool|A"
        }}
      )",
      "localhost",
      server->getListenPort());
  auto conn = std::make_unique<facebook::memcache::MemcacheInternalConnection>(
      "simple-internal-test", mcrouterOptions);
  facebook::memcache::McSetRequest request("internal");
  request.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "connection");
  folly::fibers::Baton baton;
  conn->sendRequestOne(
      request,
      [&baton](
          const facebook::memcache::McSetRequest& /* req */,
          facebook::memcache::McSetReply&& reply) {
        EXPECT_EQ(mc_res_stored, reply.result());
        baton.post();
      });
  baton.wait();
  baton.reset();
  facebook::memcache::McGetRequest getReq("internal");
  conn->sendRequestOne(
      getReq,
      [&baton](
          const facebook::memcache::McGetRequest& /* req */,
          facebook::memcache::McGetReply&& reply) {
        EXPECT_EQ(mc_res_found, reply.result());
        EXPECT_EQ("internal", folly::StringPiece(reply.value()->coalesce()));
        baton.post();
      });
  baton.wait();
  conn.reset();
  server->shutdown();
  server->join();
}

TEST(MemcachePooledConnectionTest, PooledInternalConnection) {
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  auto server = facebook::memcache::test::TestServer::create(
      false /* outOfOrder */, false /* useSsl */);
  facebook::memcache::McrouterOptions mcrouterOptions;
  mcrouterOptions.num_proxies = 1;
  mcrouterOptions.default_route = "/oregon/*/";
  mcrouterOptions.config_str = folly::sformat(
      R"(
        {{
          "pools": {{
            "A": {{
              "servers": [ "{}:{}" ],
              "protocol": "caret"
            }}
          }},
          "route": "Pool|A"
        }}
      )",
      "localhost",
      server->getListenPort());
  std::vector<std::unique_ptr<facebook::memcache::MemcacheConnection>> conns;
  for (int i = 0; i < 4; i++) {
    conns.push_back(
        std::make_unique<facebook::memcache::MemcacheInternalConnection>(
            "pooled-internal-test", mcrouterOptions));
  }
  auto pooledConn =
      std::make_unique<facebook::memcache::MemcachePooledConnection>(
          std::move(conns));
  facebook::memcache::McSetRequest request("pooled");
  request.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "internal");
  folly::fibers::Baton baton;
  pooledConn->sendRequestOne(
      request,
      [&baton](
          const facebook::memcache::McSetRequest& /* req */,
          facebook::memcache::McSetReply&& reply) {
        EXPECT_EQ(mc_res_stored, reply.result());
        baton.post();
      });
  baton.wait();
  baton.reset();
  facebook::memcache::McGetRequest getReq("pooled");
  pooledConn->sendRequestOne(
      getReq,
      [&baton](
          const facebook::memcache::McGetRequest& /* req */,
          facebook::memcache::McGetReply&& reply) {
        EXPECT_EQ(mc_res_found, reply.result());
        EXPECT_EQ("pooled", folly::StringPiece(reply.value()->coalesce()));
        baton.post();
      });
  baton.wait();
  pooledConn.reset();
  server->shutdown();
  server->join();
}
