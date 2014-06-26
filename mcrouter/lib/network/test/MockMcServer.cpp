#include <signal.h>

#include <glog/logging.h>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/test/MockMc.h"

/**
 * Mock Memcached implementation.
 *
 * The purpose of this program is to:
 *
 * 1) Provide a reference AsyncMcServer use case;
 * 2) Serve as an AsyncMcServer implementation for AsyncMcServer
 *    integration tests;
 * 3) Serve as a Memcached mock for other project's integration tests;
 * 4) Provide an easy to follow Memcached logic reference.
 *
 * The intention is to have the same semantics as our Memcached fork.
 *
 * Certain keys with __mockmc__. prefix provide extra functionality
 * useful for testing.
 */

using facebook::memcache::AsyncMcServer;
using facebook::memcache::AsyncMcServerWorker;
using facebook::memcache::McOperation;
using facebook::memcache::McReply;
using facebook::memcache::McRequest;
using facebook::memcache::McServerRequestContext;
using facebook::memcache::MockMc;
using facebook::memcache::createMcMsgRef;

class MockMcOnRequest {
 public:
  template <class Operation>
  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 Operation) {
    ctx.sendReply(McReply(mc_res_remote_error));
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_metaget>) {
    auto key = req.fullKey().str();

    auto item = mc_.get(key);
    if (!item) {
      ctx.sendReply(McReply(mc_res_notfound));
      return;
    }

    auto msg = createMcMsgRef();
    msg->result = mc_res_found;
    msg->flags = item->flags;
    msg->exptime = item->exptime;
    msg->number = 123; // FIXME: For now, testing age is set to be a constant
    inet_pton(AF_INET, "127.0.0.1", &msg->ip_addr); // FIXME
    msg->ipv = 4;

    ctx.sendReply(McReply(mc_res_found, std::move(msg)));
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_get>) {
    auto key = req.fullKey().str();

    if (key == "__mockmc__.want_busy") {
      auto msg = createMcMsgRef();
      msg->result = mc_res_busy;
      msg->err_code = SERVER_ERROR_BUSY;
      ctx.sendReply(McReply(mc_res_busy, std::move(msg)));
      return;
    } else if (key == "__mockmc__.want_try_again") {
      ctx.sendReply(McReply(mc_res_try_again));
      return;
    }

    auto item = mc_.get(key);
    if (!item) {
      ctx.sendReply(McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_found);
      reply.setValue(item->value->clone());
      reply.setFlags(item->flags);
      ctx.sendReply(std::move(reply));
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_lease_get>) {
    auto key = req.fullKey().str();

    auto out = mc_.leaseGet(key);
    McReply reply(mc_res_found);
    reply.setValue(out.first->value->clone());
    reply.setLeaseToken(out.second);
    if (out.second) {
      reply.setResult(mc_res_notfound);
    }
    ctx.sendReply(std::move(reply));
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_lease_set>) {
    auto key = req.fullKey().str();

    switch (mc_.leaseSet(key, MockMc::Item(req), req.leaseToken())) {
      case MockMc::NOT_STORED:
        ctx.sendReply(McReply(mc_res_notstored));
        return;

      case MockMc::STORED:
        ctx.sendReply(McReply(mc_res_stored));
        return;

      case MockMc::STALE_STORED:
        ctx.sendReply(McReply(mc_res_stalestored));
        return;
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_set>) {
    auto key = req.fullKey().str();

    if (key == "__mockmc__.trigger_server_error") {
      ctx.sendReply(
        McReply(mc_res_remote_error,
                "returned error msg with binary data \xdd\xab"));
      return;
    }

    mc_.set(key, MockMc::Item(req));
    ctx.sendReply(McReply(mc_res_stored));
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_add>) {
    auto key = req.fullKey().str();

    if (mc_.add(key, MockMc::Item(req))) {
      ctx.sendReply(McReply(mc_res_stored));
    } else {
      ctx.sendReply(McReply(mc_res_notstored));
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_replace>) {
    auto key = req.fullKey().str();

    if (mc_.replace(key, MockMc::Item(req))) {
      ctx.sendReply(McReply(mc_res_stored));
    } else {
      ctx.sendReply(McReply(mc_res_notstored));
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_delete>) {
    auto key = req.fullKey().str();

    if (mc_.del(key)) {
      ctx.sendReply(McReply(mc_res_deleted));
    } else {
      ctx.sendReply(McReply(mc_res_notfound));
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_incr>) {
    auto key = req.fullKey().str();
    auto p = mc_.arith(key, req.delta());
    if (!p.first) {
      ctx.sendReply(McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_stored);
      reply.setDelta(p.second);
      ctx.sendReply(std::move(reply));
    }
  }

  void onRequest(McServerRequestContext& ctx,
                 const McRequest& req,
                 McOperation<mc_op_decr>) {
    auto key = req.fullKey().str();
    auto p = mc_.arith(key, -req.delta());
    if (!p.first) {
      ctx.sendReply(McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_stored);
      reply.setDelta(p.second);
      ctx.sendReply(std::move(reply));
    }
  }

 private:
  MockMc mc_;
};

void serverLoop(size_t threadId, folly::EventBase& evb,
                AsyncMcServerWorker& worker) {
  worker.setOnRequest(MockMcOnRequest());
  evb.loop();
}

void usage(char** argv) {
  std::cerr <<
    "Arguments:\n"
    "  -P <port>      TCP port on which to listen\n"
    "  -t <fd>        TCP listen sock fd\n"
    "  -s             Use ssl\n"
    "Usage:\n"
    "  $ " << argv[0] << " -p 15213\n";
  exit(1);
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);

  AsyncMcServer::Options opts;
  opts.worker.versionString = "MockMcServer-1.0";

  bool ssl = false;
  uint16_t port = 0;

  int c;
  while ((c = getopt(argc, argv, "P:t:sh")) >= 0) {
    switch (c) {
      case 's':
        ssl = true;
        break;
      case 'P':
        port = folly::to<uint16_t>(optarg);
        break;
      case 't':
        opts.existingSocketFd = folly::to<int>(optarg);
        break;
      default:
        usage(argv);
    }
  }

  if (ssl) {
    if (port) {
      opts.sslPorts.push_back(port);
    }
    opts.pemCertPath = "mcrouter/lib/network/test/test_cert.pem";
    opts.pemKeyPath = "mcrouter/lib/network/test/test_key.pem";
    opts.pemCaPath = "mcrouter/lib/network/test/ca_cert.pem";
  } else {
    if (port) {
      opts.ports.push_back(port);
    }
  }

  try {
    LOG(INFO) << "Starting server";
    AsyncMcServer server(opts);
    server.installShutdownHandler({SIGINT, SIGTERM});
    server.spawn(&serverLoop);
    server.join();
    LOG(INFO) << "Shutting down";
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
}
