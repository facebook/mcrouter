/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <signal.h>

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterFactory.h"
#include "mcrouter/CarbonRouterInstance.h"

#include "mcrouter/config.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

namespace {
static constexpr int32_t kMaxNumProxies = 1024;
static constexpr int32_t kMaxNumClients = 1024;

void sendGetRequest(
    CarbonRouterClient<MemcacheRouterInfo>* client,
    std::string key) {
  McGetRequest cacheRequest(std::move(key));
  folly::fibers::Baton baton;
  client->send(cacheRequest, [&baton](const McGetRequest&, McGetReply&&) {
    baton.post();
  });
  baton.wait();
  return;
}
} // namespace

using Pointer = typename CarbonRouterClient<MemcacheRouterInfo>::Pointer;

static bool ValidateNumProxies(const char* flagname, int32_t value) {
  if (value > 0 && value < kMaxNumProxies) {
    return true;
  }
  XLOGF(
      ERR,
      "{}: Number of proxies must be > 0 && < {}",
      flagname,
      kMaxNumProxies);
  return false;
}
static bool ValidateNumClients(const char* flagname, int32_t value) {
  if (value > 0 && value < kMaxNumClients) {
    return true;
  }
  XLOGF(
      ERR,
      "{}: Number of clients must be > 0 && < {}",
      flagname,
      kMaxNumClients);
  return false;
}
static bool ValidateNumRequestsPerClient(const char* flagname, int32_t value) {
  if (value > 0) {
    return true;
  }
  XLOGF(ERR, "{}: Number of requests per client must be > 0", flagname);
  return false;
}

DEFINE_string(flavor, "web", "Flavor to use in CarbonRouterInstance");
DEFINE_int32(num_proxies, 2, "Number of mcrouter proxy threads to create");
DEFINE_int32(num_clients, 10, "Number of clients to create");
DEFINE_int32(num_req_per_client, 10, "Number of requests per client to send");
DEFINE_bool(
    thread_affinity,
    true,
    "Enables/disables thread affinity in CarbonRouterClient");
DEFINE_validator(num_proxies, &ValidateNumProxies);
DEFINE_validator(num_clients, &ValidateNumClients);
DEFINE_validator(num_req_per_client, &ValidateNumRequestsPerClient);

int main(int argc, char** argv) {
  folly::init(&argc, &argv);

  // Create CarbonRouterInstance
  XLOG(INFO, "Creating CarbonRouterInstance");
  std::unordered_map<std::string, std::string> flavorOverrides;
  flavorOverrides.emplace("num_proxies", std::to_string(FLAGS_num_proxies));
  flavorOverrides.emplace("asynclog_disable", "true");
  if (FLAGS_thread_affinity) {
    flavorOverrides.emplace("thread_affinity", "true");
  } else {
    flavorOverrides.emplace("thread_affinity", "false");
  }

  std::shared_ptr<CarbonRouterInstance<MemcacheRouterInfo>> router;
  try {
    router = createRouterFromFlavor<MemcacheRouterInfo>(
        FLAGS_flavor, flavorOverrides);
  } catch (std::exception& ex) {
    XLOGF(
        ERR,
        "Error creating router for threadAffinity for flavor {}. Exception: {}",
        FLAGS_flavor,
        ex.what());
    return 1;
  }

  SCOPE_EXIT {
    // Release all router resources on exit
    router->shutdown();
    freeAllRouters();
  };

  XLOGF(INFO, "Creating {} CarbonRouterClient", FLAGS_num_clients);
  // Create CarbonRouterClient's
  std::vector<Pointer> clients;
  for (int i = 0; i < FLAGS_num_clients; ++i) {
    clients.push_back(
        router->createClient(0 /* max_outstanding_requests */, false));
  }
  for (int i = 0; i < FLAGS_num_clients; ++i) {
    XLOGF(
        INFO,
        "Sending {} requests to CarbonRouterClient {}",
        FLAGS_num_req_per_client,
        i);
    for (int j = 0; j < FLAGS_num_req_per_client; ++j) {
      sendGetRequest(clients[i].get(), folly::sformat("key:{}{}", i, j));
    }
  }

  for (int i = 0; i < FLAGS_num_proxies; ++i) {
    XLOGF(
        INFO,
        "Total Connections proxy_{}: {}",
        i,
        router->getProxyBase(i)->stats().getValue(num_servers_up_stat));
  }
  return 0;
}
