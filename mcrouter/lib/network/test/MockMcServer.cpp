/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <signal.h>

#include <thread>

#include <glog/logging.h>

#include <folly/Format.h>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/test/MockMc.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

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
using facebook::memcache::IdFromType;
using facebook::memcache::McOperation;
using facebook::memcache::McReply;
using facebook::memcache::McRequestWithMcOp;
using facebook::memcache::McRequestWithOp;
using facebook::memcache::McServerRequestContext;
using facebook::memcache::MockMc;
using facebook::memcache::TReplyList;
using facebook::memcache::TRequestList;
using facebook::memcache::ThriftMsgDispatcher;
using facebook::memcache::TypedThriftReply;
using facebook::memcache::TypedThriftRequest;
using facebook::memcache::createMcMsgRef;

using namespace facebook::memcache::cpp2;

class MockMcOnRequest : public ThriftMsgDispatcher<TRequestList,
                                                   MockMcOnRequest,
                                                   McServerRequestContext&&> {
 public:
  template <class Operation>
  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithOp<Operation>&& req) {
    McServerRequestContext::reply(std::move(ctx), McReply(mc_res_remote_error));
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_metaget>&& req) {
    auto key = req.fullKey().str();

    auto item = mc_.get(key);
    if (!item) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
      return;
    }

    auto msg = createMcMsgRef();
    msg->result = mc_res_found;
    msg->flags = item->flags;
    msg->exptime = item->exptime;
    msg->number = 123; // FIXME: For now, testing age is set to be a constant
    inet_pton(AF_INET, "127.0.0.1", &msg->ip_addr); // FIXME
    msg->ipv = 4;

    McServerRequestContext::reply(std::move(ctx),
                                  McReply(mc_res_found, std::move(msg)));
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_get>&& req) {
    auto key = req.fullKey();

    if (key == "__mockmc__.want_busy") {
      auto msg = createMcMsgRef();
      msg->result = mc_res_busy;
      msg->err_code = SERVER_ERROR_BUSY;
      McServerRequestContext::reply(std::move(ctx),
                                    McReply(mc_res_busy, std::move(msg)));
      return;
    } else if (key == "__mockmc__.want_try_again") {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_try_again));
      return;
    } else if (key.startsWith("__mockmc__.want_timeout")) {
      size_t timeout = 500;
      auto argStart = key.find('(');
      if (argStart != std::string::npos) {
        timeout = folly::to<size_t>(key.subpiece(argStart + 1,
                                                 key.size() - argStart - 2));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_timeout));
      return;
    }

    auto item = mc_.get(key);
    if (!item) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_found);
      folly::IOBuf cloned;
      item->value->cloneInto(cloned);
      reply.setValue(std::move(cloned));
      reply.setFlags(item->flags);
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_lease_get>&& req) {
    auto key = req.fullKey().str();

    auto out = mc_.leaseGet(key);
    McReply reply(mc_res_found);
    folly::IOBuf cloned;
    out.first->value->cloneInto(cloned);
    reply.setValue(std::move(cloned));
    reply.setLeaseToken(out.second);
    if (out.second) {
      reply.setResult(mc_res_notfound);
    }
    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_lease_set>&& req) {
    auto key = req.fullKey().str();

    switch (mc_.leaseSet(key, MockMc::Item(req), req.leaseToken())) {
      case MockMc::LeaseSetResult::NOT_STORED:
        McServerRequestContext::reply(std::move(ctx),
                                      McReply(mc_res_notstored));
        return;

      case MockMc::LeaseSetResult::STORED:
        McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
        return;

      case MockMc::LeaseSetResult::STALE_STORED:
        McServerRequestContext::reply(std::move(ctx),
                                      McReply(mc_res_stalestored));
        return;
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_set>&& req) {
    auto key = req.fullKey().str();

    if (key == "__mockmc__.trigger_server_error") {
      McServerRequestContext::reply(std::move(ctx),
        McReply(mc_res_remote_error,
                "returned error msg with binary data \xdd\xab"));
      return;
    }

    mc_.set(key, MockMc::Item(req));
    McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_add>&& req) {
    auto key = req.fullKey().str();

    if (mc_.add(key, MockMc::Item(req))) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
    } else {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notstored));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_replace>&& req) {
    auto key = req.fullKey().str();

    if (mc_.replace(key, MockMc::Item(req))) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
    } else {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notstored));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_append>&& req) {
    auto key = req.fullKey().str();

    if (mc_.append(key, MockMc::Item(req))) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
    } else {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notstored));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_prepend>&& req) {
    auto key = req.fullKey().str();

    if (mc_.prepend(key, MockMc::Item(req))) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
    } else {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notstored));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_delete>&& req) {
    auto key = req.fullKey().str();

    if (mc_.del(key)) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_deleted));
    } else {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_touch>&& req) {
    auto key = req.fullKey().str();

    if (mc_.touch(key, req.exptime())) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_touched));
    } else {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_incr>&& req) {
    auto key = req.fullKey().str();
    auto p = mc_.arith(key, req.delta());
    if (!p.first) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_stored);
      reply.setDelta(p.second);
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_decr>&& req) {
    auto key = req.fullKey().str();
    auto p = mc_.arith(key, -req.delta());
    if (!p.first) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_stored);
      reply.setDelta(p.second);
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_flushall>&& req) {
    std::this_thread::sleep_for(std::chrono::seconds(req.number()));
    mc_.flushAll();
    McReply reply(mc_res_ok);
    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_gets>&& req) {
    auto key = req.fullKey().str();
    auto p = mc_.gets(key);
    if (!p.first) {
      McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
    } else {
      McReply reply(mc_res_found);
      folly::IOBuf cloned;
      p.first->value->cloneInto(cloned);
      reply.setValue(std::move(cloned));
      reply.setFlags(p.first->flags);
      reply.setCas(p.second);
      McServerRequestContext::reply(std::move(ctx), std::move(reply));
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_cas>&& req) {
    auto key = req.fullKey().str();
    auto ret = mc_.cas(key, MockMc::Item(req), req.cas());
    switch (ret) {
      case MockMc::CasResult::NOT_FOUND:
        McServerRequestContext::reply(std::move(ctx), McReply(mc_res_notfound));
        break;
      case MockMc::CasResult::EXISTS:
        McServerRequestContext::reply(std::move(ctx), McReply(mc_res_exists));
        break;
      case MockMc::CasResult::STORED:
        McServerRequestContext::reply(std::move(ctx), McReply(mc_res_stored));
        break;
    }
  }

  void onTypedMessage(TypedThriftRequest<McGetRequest>&& req,
                      McServerRequestContext&& ctx) {
    using Reply = TypedThriftReply<McGetReply>;
    constexpr size_t typeId = IdFromType<McGetReply, TReplyList>::value;

    Reply reply;
    auto key = req.fullKey();

    if (key == "__mockmc__.want_busy") {
      const auto errorMessage = folly::sformat("{} busy", SERVER_ERROR_BUSY);
      reply->set_result(mc_res_busy);
      reply->set_value(folly::IOBuf(folly::IOBuf::COPY_BUFFER, errorMessage));
      McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
      return;
    } else if (key == "__mockmc__.want_try_again") {
      reply->set_result(mc_res_try_again);
      McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
      return;
    } else if (key.startsWith("__mockmc__.want_timeout")) {
      reply->set_result(mc_res_timeout);
      size_t timeout = 500;
      auto argStart = key.find('(');
      if (argStart != std::string::npos) {
        timeout = folly::to<size_t>(key.subpiece(argStart + 1,
                                                 key.size() - argStart - 2));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
      McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
      return;
    }

    auto item = mc_.get(key);
    if (!item) {
      reply->set_result(mc_res_notfound);
    } else {
      reply->set_result(mc_res_found);
      folly::IOBuf cloned;
      item->value->cloneInto(cloned);
      reply->set_value(std::move(cloned));
      reply->set_flags(item->flags);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McSetRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McSetReply, TReplyList>::value;
    TypedThriftReply<McSetReply> reply;

    auto key = req.fullKey();

    if (key == "__mockmc__.trigger_server_error") {
      reply->set_result(mc_res_remote_error);
      reply->set_message("returned error msg with binary data \xdd\xab");
    } else {
      mc_.set(key, MockMc::Item(req->get_value().clone()));
      reply->set_result(mc_res_stored);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McAddRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McAddReply, TReplyList>::value;
    TypedThriftReply<McAddReply> reply;

    auto key = req.fullKey().str();

    if (mc_.add(key, MockMc::Item(req->get_value().clone()))) {
      reply->set_result(mc_res_stored);
    } else {
      reply->set_result(mc_res_notstored);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McReplaceRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McReplaceReply, TReplyList>::value;
    TypedThriftReply<McReplaceReply> reply;

    auto key = req.fullKey().str();

    if (mc_.replace(key, MockMc::Item(req->get_value().clone()))) {
      reply->set_result(mc_res_stored);
    } else {
      reply->set_result(mc_res_notstored);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McIncrRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McIncrReply, TReplyList>::value;
    TypedThriftReply<McIncrReply> reply;

    auto key = req.fullKey().str();
    auto p = mc_.arith(key, req->get_delta());
    if (!p.first) {
      reply->set_result(mc_res_notfound);
    } else {
      reply->set_result(mc_res_stored);
      reply->set_delta(p.second);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McDecrRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McDecrReply, TReplyList>::value;
    TypedThriftReply<McDecrReply> reply;

    auto key = req.fullKey().str();
    auto p = mc_.arith(key, -req->get_delta());
    if (!p.first) {
      reply->set_result(mc_res_notfound);
    } else {
      reply->set_result(mc_res_stored);
      reply->set_delta(p.second);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McDeleteRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McSetReply, TReplyList>::value;
    TypedThriftReply<McDeleteReply> reply;

    auto key = req.fullKey();

    if (mc_.del(key)) {
      reply->set_result(mc_res_deleted);
    } else {
      reply->set_result(mc_res_notfound);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McAppendRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McAppendReply, TReplyList>::value;
    TypedThriftReply<McAppendReply> reply;

    auto key = req.fullKey().str();

    if (mc_.append(key, MockMc::Item(req->get_value().clone()))) {
      reply->set_result(mc_res_stored);
    } else {
      reply->set_result(mc_res_notstored);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McPrependRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McPrependReply, TReplyList>::value;
    TypedThriftReply<McPrependReply> reply;

    auto key = req.fullKey().str();

    if (mc_.prepend(key, MockMc::Item(req->get_value().clone()))) {
      reply->set_result(mc_res_stored);
    } else {
      reply->set_result(mc_res_notstored);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  void onTypedMessage(TypedThriftRequest<McTouchRequest>&& req,
                      McServerRequestContext&& ctx) {
    constexpr size_t typeId = IdFromType<McTouchReply, TReplyList>::value;
    TypedThriftReply<McTouchReply> reply;

    auto key = req.fullKey().str();

    if (mc_.touch(key, req.exptime())) {
      reply->set_result(mc_res_touched);
    } else {
      reply->set_result(mc_res_notfound);
    }

    McServerRequestContext::reply(std::move(ctx), std::move(reply), typeId);
  }

  template <class ThriftType>
  void onTypedMessage(TypedThriftRequest<ThriftType>&&,
                      McServerRequestContext&&) {
    LOG(ERROR) << "MockMcServer does not support requests of type "
      << typeid(ThriftType).name();
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
