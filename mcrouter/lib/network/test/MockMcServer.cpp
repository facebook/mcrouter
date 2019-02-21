/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include <signal.h>

#include <iostream>
#include <thread>

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/Singleton.h>
#include <folly/logging/Init.h>

#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/CarbonMessageDispatcher.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/gen/Memcache.h"
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

using namespace facebook::memcache;

class MockMcOnRequest {
 public:
  void onRequest(McServerRequestContext&& ctx, McMetagetRequest&& req) {
    using Reply = McMetagetReply;

    auto key = req.key().fullKey().str();

    auto item = mc_.get(key);
    if (!item) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
      return;
    }

    Reply reply(carbon::Result::FOUND);
    reply.exptime() = item->exptime;
    if (key == "unknown_age") {
      reply.age() = -1;
    } else {
      reply.age() = 123;
    }
    reply.ipAddress() = "127.0.0.1";
    reply.ipv() = 4;

    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

  void onRequest(McServerRequestContext&& ctx, McGetRequest&& req) {
    using Reply = McGetReply;

    auto key = req.key().fullKey();

    if (key == "__mockmc__.want_busy") {
      Reply reply(carbon::Result::BUSY);
      reply.appSpecificErrorCode() = SERVER_ERROR_BUSY;
      reply.message() = "busy";
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
      return;
    } else if (key == "__mockmc__.want_try_again") {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::RES_TRY_AGAIN));
      return;
    } else if (key.startsWith("__mockmc__.want_timeout")) {
      size_t timeout = 500;
      auto argStart = key.find('(');
      if (argStart != std::string::npos) {
        timeout = folly::to<size_t>(
            key.subpiece(argStart + 1, key.size() - argStart - 2));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::TIMEOUT));
      return;
    }

    auto item = mc_.get(key);
    if (!item) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    } else {
      Reply reply(carbon::Result::FOUND);
      reply.value() = item->value->cloneAsValue();
      reply.flags() = item->flags;
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McGatRequest&& req) {
    using Reply = McGatReply;

    auto key = req.key().fullKey();

    if (key == "__mockmc__.want_busy") {
      Reply reply(carbon::Result::BUSY);
      reply.appSpecificErrorCode() = SERVER_ERROR_BUSY;
      reply.message() = "busy";
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
      return;
    } else if (key == "__mockmc__.want_try_again") {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::RES_TRY_AGAIN));
      return;
    } else if (key.startsWith("__mockmc__.want_timeout")) {
      size_t timeout = 500;
      auto argStart = key.find('(');
      if (argStart != std::string::npos) {
        timeout = folly::to<size_t>(
            key.subpiece(argStart + 1, key.size() - argStart - 2));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::TIMEOUT));
      return;
    }

    auto item = mc_.gat(req.exptime(), key);
    if (!item) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    } else {
      Reply reply(carbon::Result::FOUND);
      reply.value() = item->value->cloneAsValue();
      reply.flags() = item->flags;
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McLeaseGetRequest&& req) {
    using Reply = McLeaseGetReply;

    auto key = req.key().fullKey().str();

    auto out = mc_.leaseGet(key);
    Reply reply(carbon::Result::FOUND);
    reply.value() = out.first->value->cloneAsValue();
    reply.leaseToken() = out.second;
    reply.flags() = out.first->flags;
    if (out.second) {
      reply.result() = carbon::Result::NOTFOUND;
    }
    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

  void onRequest(McServerRequestContext&& ctx, McLeaseSetRequest&& req) {
    using Reply = McLeaseSetReply;

    auto key = req.key().fullKey().str();

    switch (mc_.leaseSet(key, MockMc::Item(req), req.leaseToken())) {
      case MockMc::LeaseSetResult::NOT_STORED:
        McServerRequestContext::reply(
            std::move(ctx), Reply(carbon::Result::NOTSTORED));
        return;

      case MockMc::LeaseSetResult::STORED:
        McServerRequestContext::reply(
            std::move(ctx), Reply(carbon::Result::STORED));
        return;

      case MockMc::LeaseSetResult::STALE_STORED:
        McServerRequestContext::reply(
            std::move(ctx), Reply(carbon::Result::STALESTORED));
        return;
    }
  }

  void onRequest(McServerRequestContext&& ctx, McSetRequest&& req) {
    McSetReply reply;
    auto key = req.key().fullKey().str();
    if (key == "__mockmc__.trigger_server_error") {
      reply.result() = carbon::Result::REMOTE_ERROR;
      reply.message() = "returned error msg with binary data \xdd\xab";
    } else {
      mc_.set(key, MockMc::Item(req));
      reply.result() = carbon::Result::STORED;
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

  void onRequest(McServerRequestContext&& ctx, McAddRequest&& req) {
    using Reply = McAddReply;

    auto key = req.key().fullKey().str();

    if (mc_.add(key, MockMc::Item(req))) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::STORED));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTSTORED));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McReplaceRequest&& req) {
    using Reply = McReplaceReply;

    auto key = req.key().fullKey().str();

    if (mc_.replace(key, MockMc::Item(req))) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::STORED));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTSTORED));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McAppendRequest&& req) {
    using Reply = McAppendReply;

    auto key = req.key().fullKey().str();

    if (mc_.append(key, MockMc::Item(req))) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::STORED));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTSTORED));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McPrependRequest&& req) {
    using Reply = McPrependReply;

    auto key = req.key().fullKey().str();

    if (mc_.prepend(key, MockMc::Item(req))) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::STORED));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTSTORED));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McDeleteRequest&& req) {
    using Reply = McDeleteReply;

    auto key = req.key().fullKey().str();

    if (mc_.del(key)) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::DELETED));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McTouchRequest&& req) {
    using Reply = McTouchReply;

    auto key = req.key().fullKey().str();

    if (mc_.touch(key, req.exptime())) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::TOUCHED));
    } else {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McIncrRequest&& req) {
    using Reply = McIncrReply;

    auto key = req.key().fullKey().str();
    auto p = mc_.arith(key, req.delta());
    if (!p.first) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    } else {
      Reply reply(carbon::Result::STORED);
      reply.delta() = p.second;
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McDecrRequest&& req) {
    using Reply = McDecrReply;

    auto key = req.key().fullKey().str();
    auto p = mc_.arith(key, -req.delta());
    if (!p.first) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    } else {
      Reply reply(carbon::Result::STORED);
      reply.delta() = p.second;
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McFlushAllRequest&& req) {
    using Reply = McFlushAllReply;

    std::this_thread::sleep_for(std::chrono::seconds(req.delay()));
    mc_.flushAll();
    McServerRequestContext::reply(std::move(ctx), Reply(carbon::Result::OK));
  }

  void onRequest(McServerRequestContext&& ctx, McGetsRequest&& req) {
    using Reply = McGetsReply;

    auto key = req.key().fullKey().str();
    auto p = mc_.gets(key);
    if (!p.first) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    } else {
      Reply reply(carbon::Result::FOUND);
      reply.value() = p.first->value->cloneAsValue();
      reply.flags() = p.first->flags;
      reply.casToken() = p.second;
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McGatsRequest&& req) {
    using Reply = McGatsReply;

    auto key = req.key().fullKey().str();
    auto p = mc_.gats(req.exptime(), key);
    if (!p.first) {
      McServerRequestContext::reply(
          std::move(ctx), Reply(carbon::Result::NOTFOUND));
    } else {
      Reply reply(carbon::Result::FOUND);
      reply.value() = p.first->value->cloneAsValue();
      reply.flags() = p.first->flags;
      reply.casToken() = p.second;
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx, McCasRequest&& req) {
    using Reply = McCasReply;

    auto key = req.key().fullKey().str();
    auto ret = mc_.cas(key, MockMc::Item(req), req.casToken());
    switch (ret) {
      case MockMc::CasResult::NOT_FOUND:
        McServerRequestContext::reply(
            std::move(ctx), Reply(carbon::Result::NOTFOUND));
        break;
      case MockMc::CasResult::EXISTS:
        McServerRequestContext::reply(
            std::move(ctx), Reply(carbon::Result::EXISTS));
        break;
      case MockMc::CasResult::STORED:
        McServerRequestContext::reply(
            std::move(ctx), Reply(carbon::Result::STORED));
        break;
    }
  }

  template <class Unsupported>
  void onRequest(McServerRequestContext&& ctx, Unsupported&&) {
    const std::string errorMessage = folly::sformat(
        "MockMcServer does not support {}", typeid(Unsupported).name());
    LOG(ERROR) << errorMessage;
    ReplyT<Unsupported> reply(carbon::Result::REMOTE_ERROR);
    reply.message() = std::move(errorMessage);
    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

 private:
  MockMc mc_;
};

void serverLoop(
    size_t /* threadId */,
    folly::EventBase& evb,
    AsyncMcServerWorker& worker) {
  worker.setOnRequest(MemcacheRequestHandler<MockMcOnRequest>());
  evb.loop();
}

[[noreturn]] void usage(char** argv) {
  std::cerr << "Arguments:\n"
               "  -P <port>      TCP port on which to listen\n"
               "  -t <fd>        TCP listen sock fd\n"
               "  -s             Use ssl\n"
               "  -z             Zero Copy Threshold (Default disabled)\n"
               "Usage:\n"
               "  $ "
            << argv[0] << " -p 15213\n";
  exit(1);
}

// Configure folly to enable INFO+ messages, and everything else to
// enable WARNING+.
// Set the default log handler to log asynchronously by default.
FOLLY_INIT_LOGGING_CONFIG(".=WARNING,folly=INFO; default:async=true");

int main(int argc, char** argv) {
  folly::SingletonVault::singleton()->registrationComplete();

  AsyncMcServer::Options opts;
  opts.worker.versionString = "MockMcServer-1.0";

  bool ssl = false;
  uint16_t port = 0;

  int c;
  while ((c = getopt(argc, argv, "P:t:z:sh")) >= 0) {
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
      case 'z':
        opts.worker.tcpZeroCopyThresholdBytes = folly::to<int>(optarg);
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
