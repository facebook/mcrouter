/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <folly/fibers/Baton.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/stats.h"

using facebook::memcache::McGetReply;
using facebook::memcache::McGetRequest;
using facebook::memcache::McStatsReply;
using facebook::memcache::McStatsRequest;
using facebook::memcache::MemcacheRouterInfo;
using facebook::memcache::mcrouter::CarbonRouterClient;
using facebook::memcache::mcrouter::CarbonRouterInstance;
using facebook::memcache::mcrouter::defaultTestOptions;

/**
 * This test provides an example of how to use the CarbonRouterClient API.
 *
 * The recommended usage pattern is:
 *   1. In order to use mcrouter, the client needs a CarbonRouterInstance,
 *      obtained through one of the static factory methods. In most long-lived
 *      programs, CarbonRouterInstance::init() is the way to go.
 *   2. Create a CarbonRouterClient object associated to the
 *      CarbonRouterInstance via CarbonRouterInstance::createClient() or
 *      CarbonRouterInstance::createSameThreadClient().
 *   3. Send requests through mcrouter via CarbonRouterClient::send(). (With
 *      some caveats; read the comments below.)
 */

TEST(CarbonRouterClient, basicUsageSameThreadClient) {
  // Don't log stats in tests
  auto opts = defaultTestOptions();
  opts.num_proxies = 4;
  // We only want to demonstrate client usage in this test, so reply to each
  // request with the corresponding default reply.
  opts.config_str = R"({ "route": "NullRoute" })";

  // Every program that uses mcrouter must have at least one (usually exactly
  // one) CarbonRouterInstance, which manages (re)configuration, starting up
  // request-handling proxies, stats logging, and more.
  // Using createSameThreadClient() makes most sense in situations where the
  // user controls their own EventBases, as below.
  std::vector<folly::EventBase*> evbs;
  std::vector<std::thread> threads;
  for (size_t i = 0; i < opts.num_proxies; ++i) {
    auto evb = std::make_unique<folly::EventBase>();
    evbs.push_back(evb.get());
    threads.emplace_back([evb = std::move(evb)]() { evb->loopForever(); });
  }
  auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
      "basicUsageSameThreadClient", opts, evbs);

  // When using createSameThreadClient(), users must ensure that client->send()
  // is only ever called on the same thread as the associated Proxy.
  // Note that client->send() hands the request off to the Proxy, which
  // processes/sends the request asynchronously, i.e., after client->send()
  // returns.
  // Alternatively, users may opt to obtain a client via router->createClient(),
  // in which case client->send() is thread-safe.
  //
  // In any case, we go must ensure that client will remain alive throughout the
  // entire request/reply transaction in client->send() below.
  // (router->shutdown() will complete before client is destroyed.)
  auto client =
      router->createSameThreadClient(0 /* max_outstanding_requests */);

  // Explicitly control which proxy should handle requests from this client.
  // Currently, this is necessary when using createSameThreadClient() with more
  // than one thread.
  auto& eventBase = *evbs.front();
  client->setProxyIndex(0);

  bool replyReceived = false;
  eventBase.runInEventBaseThread([ client = client.get(), &replyReceived ]() {
    // We must ensure that req will remain alive all the way through the reply
    // callback given to client->send(). This demonstrates one way of ensuring
    // this.
    auto req = std::make_unique<McGetRequest>("key");
    auto reqRawPtr = req.get();
    client->send(
        *reqRawPtr,
        [req = std::move(req), &replyReceived](
            const McGetRequest&, McGetReply&& reply) {
          EXPECT_EQ(carbon::Result::NOTFOUND, reply.result());
          replyReceived = true;
        });
  });

  // Wait for proxy threads to complete outstanding requests and exit
  // gracefully. This ensures graceful destruction of the static
  // CarbonRouterInstance instance.
  router->shutdown();
  for (auto evb : evbs) {
    evb->terminateLoopSoon();
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_TRUE(replyReceived);
}

TEST(CarbonRouterClient, basicUsageRemoteThreadClient) {
  // This test is a lot like the previous one, except this test demonstrates
  // the use of a client that can safely send a request through a Proxy
  // on another thread.  Much of the code is the exact same as before.
  auto opts = defaultTestOptions();
  opts.config_str = R"({ "route": "NullRoute" })";

  auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
      "basicUsageRemoteThreadClient", opts);

  // Create client that can safely send requests through a Proxy on another
  // thread
  auto client = router->createClient(
      0 /* max_outstanding_requests */,
      false /* max_outstanding_requests_error */);

  // Note, as in the previous test, that req is kept alive through the end of
  // the callback provided to client->send() below.
  // Also note that we are careful not to modify req while the proxy (in this
  // case, on another thread) may be processing it.
  const McGetRequest req("key");
  bool replyReceived = false;
  folly::fibers::Baton baton;

  client->send(
      req, [&baton, &replyReceived](const McGetRequest&, McGetReply&& reply) {
        EXPECT_EQ(carbon::Result::NOTFOUND, reply.result());
        replyReceived = true;
        baton.post();
      });

  // Ensure proxies have a chance to send all outstanding requests. Note the
  // extra synchronization required when using a remote-thread client.
  baton.wait();
  router->shutdown();
  EXPECT_TRUE(replyReceived);
}

TEST(CarbonRouterClient, basicUsageRemoteThreadClientThreadAffinity) {
  // This test is a lot like the previous one, except this test demonstrates
  // the use of a client that can safely send a request through a Proxy
  // on another thread with thread affinity.
  auto opts = defaultTestOptions();
  opts.config_str = R"({ "route": "NullRoute" })";
  opts.thread_affinity = true;
  opts.num_proxies = 3;

  auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
      "basicUsageRemoteThreadClientThreadAffinity", opts);

  // Create client that can safely send requests through a Proxy on another
  // thread
  auto client = router->createClient(
      0 /* max_outstanding_requests */,
      false /* max_outstanding_requests_error */);

  // Note, as in the previous test, that req is kept alive through the end of
  // the callback provided to client->send() below.
  // Also note that we are careful not to modify req while the proxy (in this
  // case, on another thread) may be processing it.
  const McGetRequest req("key");
  bool replyReceived = false;
  folly::fibers::Baton baton;

  client->send(
      req, [&baton, &replyReceived](const McGetRequest&, McGetReply&& reply) {
        EXPECT_EQ(carbon::Result::NOTFOUND, reply.result());
        replyReceived = true;
        baton.post();
      });

  // Ensure proxies have a chance to send all outstanding requests. Note the
  // extra synchronization required when using a remote-thread client.
  baton.wait();
  router->shutdown();
  EXPECT_TRUE(replyReceived);
}

TEST(CarbonRouterClient, basicUsageRemoteThreadClientThreadAffinityMulti) {
  // This test is a lot like the previous one, but it shows how to send a batch
  // of requests at once.
  auto opts = defaultTestOptions();
  opts.config_str = R"(
  {
    "route": {
      "type": "PoolRoute",
      "pool": {
        "name": "A",
        "servers": [
          "10.0.0.1:11111",
          "10.0.0.2:11111",
          "10.0.0.3:11111",
          "10.0.0.4:11111",
          "10.0.0.5:11111",
          "10.0.0.6:11111"
        ]
      }
    }
  })";
  opts.thread_affinity = true;
  opts.num_proxies = 3;
  opts.server_timeout_ms = 1;
  opts.miss_on_get_errors = true;

  auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
      "basicUsageRemoteThreadClientThreadAffinityMulti", opts);

  auto client = router->createClient(
      0 /* max_outstanding_requests */,
      false /* max_outstanding_requests_error */);

  std::vector<McGetRequest> requests{McGetRequest("key1"),
                                     McGetRequest("key2"),
                                     McGetRequest("key3"),
                                     McGetRequest("key4"),
                                     McGetRequest("key5"),
                                     McGetRequest("key6"),
                                     McGetRequest("key7"),
                                     McGetRequest("key8"),
                                     McGetRequest("key9"),
                                     McGetRequest("key10"),
                                     McGetRequest("key11"),
                                     McGetRequest("key12"),
                                     McGetRequest("key13"),
                                     McGetRequest("key14"),
                                     McGetRequest("key15")};
  folly::fibers::Baton baton;
  std::atomic<size_t> replyCount = 0;

  client->send(
      requests.begin(),
      requests.end(),
      [&baton, &replyCount, &requests](
          const McGetRequest&, McGetReply&& reply) {
        EXPECT_EQ(carbon::Result::NOTFOUND, reply.result());
        if (++replyCount == requests.size()) {
          baton.post();
        }
      });

  // Ensure proxies have a chance to send all outstanding requests. Note the
  // extra synchronization required when using a remote-thread client.
  baton.wait();
  EXPECT_EQ(requests.size(), replyCount.load());

  // Make sure that all proxies were notified, and that each proxy was notified
  // just once.
  const auto& proxies = router->getProxies();
  for (size_t i = 0; i < proxies.size(); ++i) {
    EXPECT_EQ(
        1,
        proxies[i]->stats().getValue(
            facebook::memcache::mcrouter::client_queue_notifications_stat));
  }
  EXPECT_EQ(3, proxies.size());

  router->shutdown();
}

TEST(CarbonRouterClient, remoteThreadStatsRequestUsage) {
  // This test is a lot like the previous one, except this test demonstrates
  // how to collect libmcrouter stats using the McStatsRequest.
  auto opts = defaultTestOptions();
  opts.config_str = R"({ "route": "NullRoute" })";

  auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
      "remoteThreadStatsRequestUsage", opts);

  // Create client that can safely send requests through a Proxy on another
  // thread
  auto client = router->createClient(0 /* max_outstanding_requests */);

  // Note, as in the previous test, that req is kept alive through the end of
  // the callback provided to client->send() below.
  // Also note that we are careful not to modify req while the proxy (in this
  // case, on another thread) may be processing it.
  const McStatsRequest req("all");
  bool replyReceived = false;
  folly::fibers::Baton baton;

  client->send(
      req,
      [&baton, &replyReceived](const McStatsRequest&, McStatsReply&& reply) {
        EXPECT_GT(reply.stats().size(), 1);
        EXPECT_EQ(carbon::Result::OK, reply.result());
        replyReceived = true;
        baton.post();
      });

  // Ensure proxies have a chance to send all outstanding requests. Note the
  // extra synchronization required when using a remote-thread client.
  baton.wait();
  router->shutdown();
  EXPECT_TRUE(replyReceived);
}
