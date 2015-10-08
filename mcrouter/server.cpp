/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "server.h"

#include <cstdio>
#include <signal.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/McrouterClient.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/OptionsUtil.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/ServerOnRequest.h"
#include "mcrouter/standalone_options.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

mcrouter_client_callbacks_t const server_callbacks = {
    nullptr, nullptr, nullptr};

void serverLoop(
  McrouterInstance& router,
  size_t threadId,
  folly::EventBase& evb,
  AsyncMcServerWorker& worker,
  bool retainSourceIp) {

  auto routerClient = router.createSameThreadClient(
    server_callbacks,
    &worker,
    0);
  auto proxy = router.getProxy(threadId);
  // Manually override proxy assignment
  routerClient->setProxy(proxy);

  worker.setOnRequest(ServerOnRequest(*routerClient, retainSourceIp));
  worker.setOnConnectionAccepted([proxy] () {
      stat_incr(proxy->stats, successful_client_connections_stat, 1);
      stat_incr(proxy->stats, num_clients_stat, 1);
    });
  worker.setOnConnectionCloseFinish(
      [proxy](facebook::memcache::McServerSession&) {
        stat_decr(proxy->stats, num_clients_stat, 1);
      });

  /* TODO(libevent): the only reason this is not simply evb.loop() is
     because we need to call asox stuff on every loop iteration.
     We can clean this up once we convert everything to EventBase */
  while (worker.isAlive() ||
         worker.writesPending() ||
         proxy->fiberManager.hasTasks()) {
    evb.loopOnce();
  }

  // destroy proxy on proxy thread
  router.releaseProxy(threadId);
}

}  // namespace

bool runServer(const McrouterStandaloneOptions& standaloneOpts,
               const McrouterOptions& mcrouterOpts) {
  AsyncMcServer::Options opts;

  if (standaloneOpts.listen_sock_fd >= 0) {
    opts.existingSocketFd = standaloneOpts.listen_sock_fd;
  } else if (!standaloneOpts.unix_domain_sock.empty()) {
    opts.unixDomainSockPath = standaloneOpts.unix_domain_sock;
  } else {
    opts.ports = standaloneOpts.ports;
    opts.sslPorts = standaloneOpts.ssl_ports;
    opts.pemCertPath = mcrouterOpts.pem_cert_path;
    opts.pemKeyPath = mcrouterOpts.pem_key_path;
    opts.pemCaPath = mcrouterOpts.pem_ca_path;
  }

  opts.numThreads = mcrouterOpts.num_proxies;

  if (!mcrouterOpts.debug_fifo_root.empty()) {
    opts.debugFifoPath = getServerDebugFifoFullPath(mcrouterOpts);
  }

  opts.worker.connLRUopts.maxConns =
    (standaloneOpts.max_conns + opts.numThreads - 1) / opts.numThreads;
  opts.worker.defaultVersionHandler = false;
  opts.worker.maxInFlight = standaloneOpts.max_client_outstanding_reqs;
  opts.worker.sendTimeout = std::chrono::milliseconds{
    mcrouterOpts.server_timeout_ms};

  /* Default to one read per event to help latency-sensitive workloads.
     We can make this an option if this needs to be adjusted. */
  opts.worker.maxReadsPerEvent = 1;
  opts.worker.requestsPerRead = standaloneOpts.requests_per_read;

  try {
    LOG(INFO) << "Spawning AsyncMcServer";

    AsyncMcServer server(opts);
    server.installShutdownHandler({SIGINT, SIGTERM});

    auto router = McrouterInstance::init(
      "standalone",
      mcrouterOpts,
      server.eventBases());

    if (router == nullptr) {
      LOG(ERROR) << "CRITICAL: Failed to initialize mcrouter!";
      return false;
    }

    router->addStartupOpts(standaloneOpts.toDict());

    if (standaloneOpts.postprocess_logging_route) {
      router->setPostprocessCallback(getLogPostprocessFunc<void>());
    }

    server.spawn(
      [router, &standaloneOpts] (size_t threadId,
                                 folly::EventBase& evb,
                                 AsyncMcServerWorker& worker) {
        serverLoop(*router, threadId, evb, worker,
                   standaloneOpts.retain_source_ip);
      },
      [router]() {
        router->shutdown();
      }
    );

    server.join();

    LOG(INFO) << "Shutting down";

    McrouterInstance::freeAllMcrouters();

    if (!opts.unixDomainSockPath.empty()) {
      std::remove(opts.unixDomainSockPath.c_str());
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    return false;
  }
  return true;
}

}}}  // facebook::memcache::mcrouter
