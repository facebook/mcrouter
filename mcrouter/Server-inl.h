/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <signal.h>

#include <cstdio>

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/OptionsUtil.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/ServerOnRequest.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/standalone_options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

template <class RouterInfo, template <class> class RequestHandler>
void serverLoop(
    CarbonRouterInstance<RouterInfo>& router,
    size_t threadId,
    folly::EventBase& evb,
    AsyncMcServerWorker& worker,
    const McrouterStandaloneOptions& standaloneOpts) {
  using RequestHandlerType = RequestHandler<ServerOnRequest<RouterInfo>>;

  auto routerClient =
      router.createSameThreadClient(0 /* maximum_outstanding_requests */);

  auto proxy = router.getProxy(threadId);
  // Manually override proxy assignment
  routerClient->setProxy(proxy);

  worker.setOnRequest(
      RequestHandlerType(*routerClient, standaloneOpts.retain_source_ip));
  worker.setOnConnectionAccepted([proxy]() {
    proxy->stats().increment(successful_client_connections_stat);
    proxy->stats().increment(num_clients_stat);
  });
  worker.setOnConnectionCloseFinish(
      [proxy](facebook::memcache::McServerSession&) {
        proxy->stats().decrement(num_clients_stat);
      });

  // Setup compression on each worker.
  if (standaloneOpts.enable_server_compression) {
    auto codecManager = router.getCodecManager();
    if (codecManager) {
      worker.setCompressionCodecMap(codecManager->getCodecMap());
    } else {
      LOG(WARNING) << "Compression is enabled but couldn't find CodecManager. "
                   << "Compression will be disabled.";
    }
  }

  /* TODO(libevent): the only reason this is not simply evb.loop() is
     because we need to call asox stuff on every loop iteration.
     We can clean this up once we convert everything to EventBase */
  while (worker.isAlive() || worker.writesPending()) {
    evb.loopOnce();
  }
}

} // detail

template <class RouterInfo, template <class> class RequestHandler>
bool runServer(
    const McrouterStandaloneOptions& standaloneOpts,
    const McrouterOptions& mcrouterOpts) {
  AsyncMcServer::Options opts;

  if (standaloneOpts.listen_sock_fd >= 0) {
    opts.existingSocketFd = standaloneOpts.listen_sock_fd;
  } else if (!standaloneOpts.unix_domain_sock.empty()) {
    opts.unixDomainSockPath = standaloneOpts.unix_domain_sock;
  } else {
    opts.ports = standaloneOpts.ports;
    opts.sslPorts = standaloneOpts.ssl_ports;
    opts.tlsTicketKeySeedPath = standaloneOpts.tls_ticket_key_seed_path;
    opts.pemCertPath = mcrouterOpts.pem_cert_path;
    opts.pemKeyPath = mcrouterOpts.pem_key_path;
    opts.pemCaPath = mcrouterOpts.pem_ca_path;
    opts.tfoEnabledForSsl = mcrouterOpts.enable_ssl_tfo;
    opts.tfoQueueSize = standaloneOpts.tfo_queue_size;
  }

  opts.numThreads = mcrouterOpts.num_proxies;

  opts.setPerThreadMaxConns(standaloneOpts.max_conns, opts.numThreads);
  opts.tcpListenBacklog = standaloneOpts.tcp_listen_backlog;
  opts.worker.defaultVersionHandler = false;
  opts.worker.maxInFlight = standaloneOpts.max_client_outstanding_reqs;
  opts.worker.sendTimeout =
      std::chrono::milliseconds{standaloneOpts.client_timeout_ms};
  if (!mcrouterOpts.debug_fifo_root.empty()) {
    opts.worker.debugFifoPath = getServerDebugFifoFullPath(mcrouterOpts);
  }

  if (standaloneOpts.server_load_interval_ms > 0) {
    opts.cpuControllerOpts.dataCollectionInterval =
        std::chrono::milliseconds(standaloneOpts.server_load_interval_ms);
    opts.cpuControllerOpts.enableServerLoad = true;
    opts.cpuControllerOpts.target = 0; // Disable drop probability.
  }

  /* Default to one read per event to help latency-sensitive workloads.
     We can make this an option if this needs to be adjusted. */
  opts.worker.maxReadsPerEvent = 1;

  try {
    LOG(INFO) << "Spawning AsyncMcServer";

    AsyncMcServer server(opts);
    server.installShutdownHandler({SIGINT, SIGTERM});

    CarbonRouterInstance<RouterInfo>* router = nullptr;

    SCOPE_EXIT {
      if (router) {
        router->shutdown();
      }
      server.join();

      LOG(INFO) << "Shutting down";

      freeAllRouters();

      if (!opts.unixDomainSockPath.empty()) {
        std::remove(opts.unixDomainSockPath.c_str());
      }
    };

    router = CarbonRouterInstance<RouterInfo>::init(
        "standalone", mcrouterOpts, server.eventBases());
    if (router == nullptr) {
      LOG(ERROR) << "CRITICAL: Failed to initialize mcrouter!";
      return false;
    }

    router->addStartupOpts(standaloneOpts.toDict());

    if (standaloneOpts.postprocess_logging_route) {
      router->setPostprocessCallback(getLogPostprocessFunc<void>());
    }

    if (standaloneOpts.enable_server_compression &&
        !mcrouterOpts.enable_compression) {
      initCompression(*router);
    }

    folly::Baton<> shutdownBaton;
    server.spawn(
        [router, &standaloneOpts](
            size_t threadId,
            folly::EventBase& evb,
            AsyncMcServerWorker& worker) {
          detail::serverLoop<RouterInfo, RequestHandler>(
              *router, threadId, evb, worker, standaloneOpts);
        },
        [&shutdownBaton]() { shutdownBaton.post(); });

    shutdownBaton.wait();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    return false;
  }
  return true;
}

} // mcrouter
} // memcache
} // facebook
