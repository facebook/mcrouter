/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
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
#include "mcrouter/config.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

using facebook::memcache::McGetReply;
using facebook::memcache::McGetRequest;
using facebook::memcache::MemcacheRouterInfo;
using facebook::memcache::mcrouter::defaultTestOptions;
using facebook::memcache::mcrouter::CarbonRouterClient;
using facebook::memcache::mcrouter::CarbonRouterInstance;

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
      "sameThreadClientTest", opts, evbs);

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
  auto* proxy = router->getProxy(0);
  client->setProxy(proxy);

  bool replyReceived = false;
  eventBase.runInEventBaseThread([ client = client.get(), &replyReceived ]() {
    // We must ensure that req will remain alive all the way through the reply
    // callback given to client->send(). This demonstrates one way of ensuring
    // this.
    auto req = std::make_unique<McGetRequest>("key");
    auto reqRawPtr = req.get();
    client->send(
        *reqRawPtr,
        [ req = std::move(req), &replyReceived ](
            const McGetRequest&, McGetReply&& reply) {
          EXPECT_EQ(mc_res_notfound, reply.result());
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
      "remoteThreadClientTest", opts);

  // Create client that can safely send requests through a Proxy on another
  // thread
  auto client = router->createClient(0 /* max_outstanding_requests */);

  // Note, as in the previous test, that req is kept alive through the end of
  // the callback provided to client->send() below.
  // Also note that we are careful not to modify req while the proxy (in this
  // case, on another thread) may be processing it.
  const McGetRequest req("key");
  bool replyReceived = false;
  folly::fibers::Baton baton;

  client->send(
      req, [&baton, &replyReceived](const McGetRequest&, McGetReply&& reply) {
        EXPECT_EQ(mc_res_notfound, reply.result());
        replyReceived = true;
        baton.post();
      });

  // Ensure proxies have a chance to send all outstanding requests. Note the
  // extra synchronization required when using a remote-thread client.
  baton.wait();
  router->shutdown();
  EXPECT_TRUE(replyReceived);
}
