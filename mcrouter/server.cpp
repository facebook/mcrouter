#include "server.h"

#include <signal.h>

#include "mcrouter/ProxyThread.h"
#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/mcrouter_client.h"
#include "mcrouter/proxy.h"
#include "mcrouter/standalone_options.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "thrift/lib/cpp/transport/TTransportException.h"

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
  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<M>) {
    mcrouter_msg_t router_msg;

    auto op = mc_op_t(M);
    /* TODO: nasty C/C++ interface stuff.  We should hand off the McRequest
       directly here.  For now, hand off the dependentMsg() since we can assume
       req will stay alive. */
    router_msg.req = const_cast<mc_msg_t*>(req.dependentMsg(op).get());
    router_msg.reply = nullptr;
    router_msg.result = mc_res_unknown;
    router_msg.context = &ctx;

    mcrouter_send(client_, &router_msg, 1);
  }

 private:
  mcrouter_client_t* client_;
};

void router_on_reply(mcrouter_client_t *client,
                     mcrouter_msg_t* msg,
                     void *context) {
  auto reqCtx = reinterpret_cast<McServerRequestContext*>(msg->context);

  assert(reqCtx);
  /* TODO: this is nasty C/C++ interface code. Convert everything
     to McRequest/McReply throughout. */
  reqCtx->sendReply(McReply(msg->reply->result,
                            McMsgRef::cloneRef(msg->reply)));
  msg->context = nullptr;
}

mcrouter_client_callbacks_t const server_callbacks = {
  router_on_reply,  // on_reply callback
  nullptr
};

void serverLoop(
  mcrouter_t& router,
  size_t threadId,
  folly::EventBase& evb,
  AsyncMcServerWorker& worker) {

  auto routerClient =
    mcrouter_client_new(&router,
                        &evb,
                        server_callbacks,
                        &worker,
                        0, false);
  auto proxy = router.proxy_threads[threadId]->proxy;
  proxy->attachEventBase(&evb);
  // Manually override proxy assignment
  routerClient->proxy = proxy;

  worker.setOnRequest(ServerOnRequest(routerClient));
  worker.setOnConnectionAccepted([proxy] () {
      stat_incr(proxy, successful_client_connections_stat, 1);
      stat_incr(proxy, num_clients_stat, 1);
    });
  worker.setOnConnectionClosed([proxy] () {
      stat_decr(proxy, num_clients_stat, 1);
    });

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
      [&router] (size_t threadId,
                 folly::EventBase& evb,
                 AsyncMcServerWorker& worker) {
        serverLoop(router, threadId, evb, worker);
      }
    );

    server.installShutdownHandler({SIGINT, SIGTERM});
    server.join();

    LOG(INFO) << "Shutting down";
  } catch (const apache::thrift::transport::TTransportException& e) {
    LOG(ERROR) << e.what();
  }
}

}}}  // facebook::memcache::mcrouter
