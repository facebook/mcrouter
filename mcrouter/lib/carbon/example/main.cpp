/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <chrono>

#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>
#include <glog/logging.h>

#include "mcrouter/lib/carbon/example/gen/HelloGoodbye.h"
#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeRouterInfo.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/config.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;
using namespace hellogoodbye;

namespace {

constexpr uint16_t kPort = 11303;
constexpr uint16_t kPort2 = 11305;

struct HelloGoodbyeOnRequest {
  void onRequest(McServerRequestContext&& ctx, HelloRequest&& request) {
    LOG(INFO) << "Hello! Server " << reinterpret_cast<uintptr_t>(this)
              << " got key " << request.key().fullKey().str();
    McServerRequestContext::reply(
        std::move(ctx), HelloReply(carbon::Result::OK));
  }

  void onRequest(McServerRequestContext&& ctx, GoodbyeRequest&& request) {
    LOG(INFO) << "Good bye! Server " << reinterpret_cast<uintptr_t>(this)
              << " got key " << request.key().fullKey().str();
    McServerRequestContext::reply(
        std::move(ctx), GoodbyeReply(carbon::Result::OK));
  }
};

inline void spawnServer(AsyncMcServer& server) {
  server.spawn([](
      size_t /* threadId */,
      folly::EventBase& evb,
      AsyncMcServerWorker& worker) {
    worker.setOnRequest(HelloGoodbyeRequestHandler<HelloGoodbyeOnRequest>());

    while (worker.isAlive() || worker.writesPending()) {
      evb.loopOnce();
    }
  });
}

AsyncMcServer::Options getOpts(uint16_t port) {
  AsyncMcServer::Options opts;
  opts.worker.debugFifoPath = "./hello-goodbye";
  opts.ports.push_back(port);
  opts.numThreads = 4;
  return opts;
}

void testClientServer() {
  // Run simple HelloGoodbye server
  AsyncMcServer server(getOpts(kPort));
  spawnServer(server);

  // Send a few Hello/Goodbye requests
  folly::EventBase evb;
  AsyncMcClient client(
      evb, ConnectionOptions("localhost", kPort, mc_caret_protocol));
  auto& fm = folly::fibers::getFiberManager(evb);
  for (size_t i = 0; i < 100; ++i) {
    using namespace std::chrono_literals;

    if (i % 2 == 0) {
      fm.addTask([&client, i]() {
        auto reply =
            client.sendSync(HelloRequest(folly::sformat("key:{}", i)), 200ms);
        if (reply.result() != carbon::Result::OK) {
          LOG(ERROR) << "Unexpected result: "
                     << carbon::resultToString(reply.result());
        }

      });
    } else {
      fm.addTask([&client, i]() {
        auto reply =
            client.sendSync(GoodbyeRequest(folly::sformat("key:{}", i)), 200ms);
        if (reply.result() != carbon::Result::OK) {
          LOG(ERROR) << "Unexpected result: "
                     << carbon::resultToString(reply.result());
        }
      });
    }
  }

  while (fm.hasTasks()) {
    evb.loopOnce();
  }

  // Shutdown server
  server.shutdown();
  server.join();
}

void sendHelloRequestSync(
    CarbonRouterClient<HelloGoodbyeRouterInfo>* client,
    std::string key) {
  HelloRequest req(std::move(key));
  folly::fibers::Baton baton;

  client->send(req, [&baton](const HelloRequest&, HelloReply&& reply) {
    LOG(INFO) << "Reply received! Result: "
              << carbon::resultToString(reply.result());
    baton.post();
  });

  // Ensure proxies have a chance to send all outstanding requests. Note the
  // extra synchronization required when using a remote-thread client.
  baton.wait();
}

void testRouter() {
  // Run 2 simple HelloGoodbye server
  AsyncMcServer server1(getOpts(kPort));
  spawnServer(server1);
  AsyncMcServer server2(getOpts(kPort2));
  spawnServer(server2);

  // Start mcrouter
  McrouterOptions routerOpts;
  routerOpts.num_proxies = 2;
  routerOpts.asynclog_disable = true;
  // routerOpts.config_str = R"({"route": "NullRoute"})";
  routerOpts.config_str = R"(
  {
    "pools": {
      "A": {
        "servers": [ "127.0.0.1:11303", "127.0.0.1:11305" ],
        "protocol": "caret"
      }
    },
    "route": {
      "type": "DuplicateRoute",
      "target": "PoolRoute|A",
      "copies": 5
    }
  }
  )";

  auto router = CarbonRouterInstance<HelloGoodbyeRouterInfo>::init(
      "remoteThreadClientTest", routerOpts);
  if (!router) {
    LOG(ERROR) << "Failed to initialize router!";
    return;
  }

  SCOPE_EXIT {
    // Release all router resources on exit
    router->shutdown();
    server1.shutdown();
    server1.join();
    server2.shutdown();
    server2.join();
    freeAllRouters();
  };

  for (int i = 0; i < 10; ++i) {
    auto client = router->createClient(0 /* max_outstanding_requests */);
    sendHelloRequestSync(client.get(), folly::sformat("key:{}", i));
  }
}

void testCarbonLookasideRouter() {
  // Run 2 simple HelloGoodbye server
  AsyncMcServer server1(getOpts(kPort));
  spawnServer(server1);
  AsyncMcServer server2(getOpts(kPort2));
  spawnServer(server2);

  // Start mcrouter
  McrouterOptions routerOpts;
  routerOpts.num_proxies = 2;
  routerOpts.asynclog_disable = true;
  routerOpts.config_str = R"(
  {
    "pools": {
      "A": {
        "servers": [ "127.0.0.1:11303", "127.0.0.1:11305" ],
        "protocol": "caret"
      }
    },
    "route": {
      "type": "CarbonLookasideRoute",
      "prefix": "petra",
      "ttl": 100,
      "child": {
        "type": "DuplicateRoute",
        "target": "PoolRoute|A",
        "copies": 5
      }
    }
  }
  )";

  auto router = CarbonRouterInstance<HelloGoodbyeRouterInfo>::init(
      "remoteThreadClientTest", routerOpts);
  if (!router) {
    LOG(ERROR) << "Failed to initialize router!";
    return;
  }

  SCOPE_EXIT {
    // Release all router resources on exit
    router->shutdown();
    server1.shutdown();
    server1.join();
    server2.shutdown();
    server2.join();
    freeAllRouters();
  };

  auto client = router->createClient(0 /* max_outstanding_requests */);
  for (int i = 0; i < 10; ++i) {
    sendHelloRequestSync(client.get(), folly::sformat("key:{}", i));
  }

  for (int i = 0; i < 10; ++i) {
    sendHelloRequestSync(client.get(), folly::sformat("key:{}", i));
  }
}

} // anonymous

int main(int argc, char* argv[]) {
  folly::init(&argc, &argv);

  // testClientServer();
  testRouter();
  testCarbonLookasideRouter();

  return 0;
}
