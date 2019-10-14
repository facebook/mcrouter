/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <signal.h>

#include <cstdio>

#include <folly/io/async/EventBase.h>

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/OptionsUtil.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/ServerOnRequest.h"
#include "mcrouter/StandaloneConfig.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/standalone_options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

inline std::function<void(McServerSession&)> getAclChecker(
    const McrouterOptions& opts,
    const McrouterStandaloneOptions& standaloneOpts) {
  if (standaloneOpts.acl_checker_enable) {
    try {
      return getConnectionAclChecker(
          standaloneOpts.server_ssl_service_identity,
          standaloneOpts.acl_checker_enforce);
    } catch (const std::exception& ex) {
      MC_LOG_FAILURE(
          opts,
          failure::Category::kSystemError,
          "Error creating acl checker: {}",
          ex.what());
      LOG(WARNING) << "Disabling acl checker on all threads.";
    }
  }
  return [](McServerSession&) {};
}

template <class RouterInfo, template <class> class RequestHandler>
void serverLoop(
    CarbonRouterInstance<RouterInfo>& router,
    size_t threadId,
    folly::EventBase& evb,
    AsyncMcServerWorker& worker,
    const McrouterStandaloneOptions& standaloneOpts) {
  using RequestHandlerType = RequestHandler<ServerOnRequest<RouterInfo>>;

  auto routerClient = standaloneOpts.remote_thread
      ? router.createClient(0 /* maximum_outstanding_requests */)
      : router.createSameThreadClient(0 /* maximum_outstanding_requests */);

  auto proxy = router.getProxy(threadId);
  // Manually override proxy assignment
  routerClient->setProxyIndex(threadId);

  worker.setOnRequest(RequestHandlerType(
      *routerClient,
      evb,
      standaloneOpts.retain_source_ip,
      standaloneOpts.enable_pass_through_mode,
      standaloneOpts.remote_thread));

  worker.setOnConnectionAccepted(
      [proxy,
       aclChecker = getAclChecker(proxy->router().opts(), standaloneOpts)](
          McServerSession& session) mutable {
        proxy->stats().increment(num_client_connections_stat);
        try {
          aclChecker(session);
        } catch (const std::exception& ex) {
          MC_LOG_FAILURE(
              proxy->router().opts(),
              failure::Category::kSystemError,
              "Error running acl checker: {}",
              ex.what());
          LOG(WARNING) << "Disabling acl checker on this thread.";
          aclChecker = [](McServerSession&) {};
        }
      });
  worker.setOnConnectionCloseFinish(
      [proxy](McServerSession&, bool onAcceptedCalled) {
        if (onAcceptedCalled) {
          proxy->stats().decrement(num_client_connections_stat);
        }
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

} // namespace detail

template <class RouterInfo, template <class> class RequestHandler>
bool runServer(
    const McrouterOptions& mcrouterOpts,
    const McrouterStandaloneOptions& standaloneOpts,
    StandalonePreRunCb preRunCb) {
  AsyncMcServer::Options opts;

  if (standaloneOpts.listen_sock_fd >= 0) {
    opts.existingSocketFd = standaloneOpts.listen_sock_fd;
  } else if (!standaloneOpts.unix_domain_sock.empty()) {
    opts.unixDomainSockPath = standaloneOpts.unix_domain_sock;
  } else {
    opts.listenAddresses = standaloneOpts.listen_addresses;
    opts.ports = standaloneOpts.ports;
    opts.sslPorts = standaloneOpts.ssl_ports;
    opts.tlsTicketKeySeedPath = standaloneOpts.tls_ticket_key_seed_path;
    opts.pemCertPath = standaloneOpts.server_pem_cert_path;
    opts.pemKeyPath = standaloneOpts.server_pem_key_path;
    opts.pemCaPath = standaloneOpts.server_pem_ca_path;
    opts.sslRequirePeerCerts = standaloneOpts.ssl_require_peer_certs;
    opts.tfoEnabledForSsl = mcrouterOpts.enable_ssl_tfo;
    opts.tfoQueueSize = standaloneOpts.tfo_queue_size;
    opts.worker.useKtls12 = standaloneOpts.ssl_use_ktls12;
  }

  opts.numThreads = mcrouterOpts.num_proxies;
  opts.numListeningSockets = standaloneOpts.num_listening_sockets;
  opts.worker.tcpZeroCopyThresholdBytes =
      standaloneOpts.tcp_zero_copy_threshold;

  size_t maxConns =
      opts.setMaxConnections(standaloneOpts.max_conns, opts.numThreads);
  if (maxConns > 0) {
    VLOG(1) << "The system will allow " << maxConns
            << " simultaneos connections before start closing connections"
            << " using an LRU algorithm";
  }

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

    if (standaloneOpts.remote_thread) {
      router =
          CarbonRouterInstance<RouterInfo>::init("standalone", mcrouterOpts);
    } else {
      router = CarbonRouterInstance<RouterInfo>::init(
          "standalone", mcrouterOpts, server.eventBases());
    }
    if (router == nullptr) {
      LOG(ERROR) << "CRITICAL: Failed to initialize mcrouter!";
      return false;
    }

    router->addStartupOpts(standaloneOpts.toDict());

    if (standaloneOpts.enable_server_compression &&
        !mcrouterOpts.enable_compression) {
      initCompression(*router);
    }

    if (preRunCb) {
      preRunCb(*router);
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

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
