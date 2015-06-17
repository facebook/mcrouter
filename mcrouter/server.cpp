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

#include "mcrouter/config.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/ManagedModeUtil.h"
#include "mcrouter/McrouterClient.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
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
  explicit ServerOnRequest(
    McrouterClient* client,
    bool retainSourceIp = false)
      : client_(client),
        retainSourceIp_(retainSourceIp) {
  }

  template <int M>
  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<M>) {
    mcrouter_msg_t router_msg;
    auto& peerIp = ctx.session().peerIpAddress();
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
    if (retainSourceIp_) {
      client_->send(&router_msg, 1, peerIp);
    } else {
      client_->send(&router_msg, 1);
    }
    p.release();
  }

 private:
  McrouterClient* client_;
  bool retainSourceIp_{false};
};

void router_on_reply(mcrouter_msg_t* msg,
                     void* context) {
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
  McrouterInstance& router,
  size_t threadId,
  folly::EventBase& evb,
  AsyncMcServerWorker& worker,
  bool managedMode,
  bool retainSourceIp) {

  auto routerClient = router.createSameThreadClient(
    server_callbacks,
    &worker,
    0);
  auto proxy = router.getProxy(threadId);
  // Manually override proxy assignment
  routerClient->setProxy(proxy);

  worker.setOnRequest(ServerOnRequest(routerClient.get(), retainSourceIp));
  worker.setOnConnectionAccepted([proxy] () {
      stat_incr(proxy->stats, successful_client_connections_stat, 1);
      stat_incr(proxy->stats, num_clients_stat, 1);
    });
  worker.setOnConnectionCloseFinish(
      [proxy](facebook::memcache::McServerSession&) {
        stat_decr(proxy->stats, num_clients_stat, 1);
      });
  if (managedMode) {
    worker.setOnShutdownOperation([&router] () {
        if (!shutdownFromChild()) {
          MC_LOG_FAILURE(router.opts(), failure::Category::kSystemError,
                         "Could not shutdown mcrouter on user command.");
        }
      });
  }

  /* TODO(libevent): the only reason this is not simply evb.loop() is
     because we need to call asox stuff on every loop iteration.
     We can clean this up once we convert everything to EventBase */
  while (worker.isAlive() ||
         worker.writesPending() ||
         proxy->fiberManager.hasTasks()) {
    evb.loopOnce();
  }
}

}  // namespace

bool runServer(const McrouterStandaloneOptions& standaloneOpts,
               const McrouterOptions& mcrouterOpts) {
  AsyncMcServer::Options opts;

  if (standaloneOpts.listen_sock_fd >= 0) {
    opts.existingSocketFd = standaloneOpts.listen_sock_fd;
  } else {
    opts.ports = standaloneOpts.ports;
    opts.sslPorts = standaloneOpts.ssl_ports;
    opts.pemCertPath = mcrouterOpts.pem_cert_path;
    opts.pemKeyPath = mcrouterOpts.pem_key_path;
    opts.pemCaPath = mcrouterOpts.pem_ca_path;
  }

  opts.numThreads = mcrouterOpts.num_proxies;

  opts.worker.versionString = MCROUTER_PACKAGE_STRING;
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

    server.spawn(
      [router, &standaloneOpts] (size_t threadId,
                                 folly::EventBase& evb,
                                 AsyncMcServerWorker& worker) {
        serverLoop(*router, threadId, evb, worker, standaloneOpts.managed,
                   standaloneOpts.retain_source_ip);
      }
    );

    server.join();

    LOG(INFO) << "Shutting down";

    McrouterInstance::freeAllMcrouters();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    return false;
  }
  return true;
}

}}}  // facebook::memcache::mcrouter
