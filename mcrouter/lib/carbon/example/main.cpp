/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <chrono>

#include <folly/fibers/FiberManagerMap.h>
#include <folly/io/async/EventBase.h>
#include <glog/logging.h>

#include "mcrouter/lib/carbon/example/gen/HelloGoodbye.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"

using namespace facebook::memcache;
using namespace hellogoodbye;

namespace {
constexpr uint16_t kPort = 11303;
} // anonymous

struct HelloGoodbyeOnRequest {
  void onRequest(McServerRequestContext&& ctx, HelloRequest&& request) {
    LOG(INFO) << "Hello! Got key " << request.key().fullKey().str();
    McServerRequestContext::reply(std::move(ctx), HelloReply(mc_res_ok));
  }

  void onRequest(McServerRequestContext&& ctx, GoodbyeRequest&& request) {
    LOG(INFO) << "Good bye! Got key " << request.key().fullKey().str();
    McServerRequestContext::reply(std::move(ctx), GoodbyeReply(mc_res_ok));
  }
};

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);

  AsyncMcServer::Options opts;
  opts.ports.push_back(kPort);
  opts.numThreads = 4;

  // Run simple HelloGoodbye server
  AsyncMcServer server(opts);
  server.spawn([](
      size_t threadId, folly::EventBase& evb, AsyncMcServerWorker& worker) {
    worker.setOnRequest(HelloGoodbyeRequestHandler<HelloGoodbyeOnRequest>());

    while (worker.isAlive() || worker.writesPending()) {
      evb.loopOnce();
    }
  });

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
        if (reply.result() != mc_res_ok) {
          LOG(ERROR) << "Unexpected result: "
                     << mc_res_to_string(reply.result());
        }

      });
    } else {
      fm.addTask([&client, i]() {
        auto reply =
            client.sendSync(GoodbyeRequest(folly::sformat("key:{}", i)), 200ms);
        if (reply.result() != mc_res_ok) {
          LOG(ERROR) << "Unexpected result: "
                     << mc_res_to_string(reply.result());
        }
      });
    }
  }

  while (fm.hasTasks()) {
    evb.loopOnce();
  }

  server.shutdown();
  server.join();

  return 0;
}
