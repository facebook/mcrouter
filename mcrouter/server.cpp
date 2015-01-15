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

#include <signal.h>

#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/ManagedModeUtil.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/mcrouter_client.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/standalone_options.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

/**
 * Server callback for standalone Mcrouter
 */
class ServerOnRequest {
 public:
  explicit ServerOnRequest(mcrouter_client_t* client)
      : client_(client) {
  }

  template <int M>
  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<M>) {
    mcrouter_msg_t router_msg;

    auto op = mc_op_t(M);
    /* TODO: nasty C/C++ interface stuff.  We should hand off the McRequest
       directly here.  For now, hand off the dependentMsg() since we can assume
       req will stay alive. */
    auto p = folly::make_unique<McServerRequestContext>(std::move(ctx));
    router_msg.context = p.get();
    router_msg.saved_request = std::move(req);
    auto msg = router_msg.saved_request->dependentMsg(op);
    router_msg.req = const_cast<mc_msg_t*>(msg.get());
    /* mcrouter_send will incref req, it's ok to destroy msg after this call */
    mcrouter_send(client_, &router_msg, 1);
    p.release();
  }

 private:
  mcrouter_client_t* client_;
};

void router_on_reply(mcrouter_client_t *client,
                     mcrouter_msg_t* msg,
                     void *context) {
  std::unique_ptr<McServerRequestContext> p(
    reinterpret_cast<McServerRequestContext*>(msg->context));

  assert(p.get());
  McServerRequestContext::reply(std::move(*p),
                                std::move(msg->reply));
  msg->context = nullptr;
}

mcrouter_client_callbacks_t const server_callbacks = {
  router_on_reply,  // on_reply callback
  nullptr,
  nullptr
};

void serverLoop(
  mcrouter_t& router,
  size_t threadId,
  folly::EventBase& evb,
  AsyncMcServerWorker& worker,
  bool managedMode) {

  auto routerClient =
    mcrouter_client_new(&router,
                        server_callbacks,
                        &worker,
                        0);
  auto proxy = router.getProxy(threadId);
  proxy->attachEventBase(&evb);
  // Manually override proxy assignment
  routerClient->proxy = proxy;

  worker.setOnRequest(ServerOnRequest(routerClient));
  worker.setOnConnectionAccepted([proxy] () {
      stat_incr(proxy->stats, successful_client_connections_stat, 1);
      stat_incr(proxy->stats, num_clients_stat, 1);
    });
  worker.setOnConnectionClosed([proxy] (facebook::memcache::McServerSession&) {
      stat_decr(proxy->stats, num_clients_stat, 1);
    });
  if (managedMode) {
    worker.setOnShutdownOperation([&router] () {
        if (!shutdownFromChild()) {
          logFailure(&router, failure::Category::kSystemError,
                     "Could not shutdown mcrouter on user command.");
        }
      });
  }

  /* TODO(libevent): the only reason this is not simply evb.loop() is
     because we need to call asox stuff on every loop iteration.
     We can clean this up once we convert everything to EventBase */
  while (worker.isAlive() || worker.writesPending()) {
    mcrouterLoopOnce(&evb);
  }

  mcrouter_client_disconnect(routerClient);
}

}  // namespace

void runServer(const McrouterStandaloneOptions& standaloneOpts,
               mcrouter_t& router) {
  AsyncMcServer::Options opts;

  if (standaloneOpts.listen_sock_fd >= 0) {
    opts.existingSocketFd = standaloneOpts.listen_sock_fd;
  } else {
    opts.ports = standaloneOpts.ports;
    opts.sslPorts = standaloneOpts.ssl_ports;
    opts.pemCertPath = router.opts.pem_cert_path;
    opts.pemKeyPath = router.opts.pem_key_path;
    opts.pemCaPath = router.opts.pem_ca_path;
  }

  opts.numThreads = router.opts.num_proxies;

  opts.worker.versionString = MCROUTER_PACKAGE_STRING;
  opts.worker.maxInFlight = standaloneOpts.max_client_outstanding_reqs;
  opts.worker.sendTimeout = std::chrono::milliseconds{
    router.opts.server_timeout_ms};

  /* Default to one read per event to help latency-sensitive workloads.
     We can make this an option if this needs to be adjusted. */
  opts.worker.maxReadsPerEvent = 1;
  opts.worker.requestsPerRead = standaloneOpts.requests_per_read;

  try {
    LOG(INFO) << "Spawning AsyncMcServer";

    AsyncMcServer server(opts);
    server.spawn(
      [&router, &standaloneOpts] (size_t threadId,
                                  folly::EventBase& evb,
                                  AsyncMcServerWorker& worker) {
        serverLoop(router, threadId, evb, worker, standaloneOpts.managed);
      }
    );

    server.installShutdownHandler({SIGINT, SIGTERM});
    server.join();

    LOG(INFO) << "Shutting down";
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
}

}}}  // facebook::memcache::mcrouter
