/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/config.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/McrouterClient.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class Request>
struct ServerRequestContext {
  McServerRequestContext ctx;
  Request req;

  ServerRequestContext(McServerRequestContext&& ctx_, Request&& req_)
      : ctx(std::move(ctx_)), req(std::move(req_)) {}
};

class ServerOnRequest : public ThriftMsgDispatcher<TRequestList,
                                                   ServerOnRequest,
                                                   McServerRequestContext&&> {
 public:
  template <class Request>
  using ReplyFunction = void (*)(McServerRequestContext&& ctx,
                                 ReplyT<Request>&& reply);

  ServerOnRequest(McrouterClient& client, bool retainSourceIp)
    : client_(client),
      retainSourceIp_(retainSourceIp) {}

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_version>&& req) {
    McServerRequestContext::reply(std::move(ctx),
                                  McReply(mc_res_ok, MCROUTER_PACKAGE_STRING));
  }

  template <int op>
  void onRequest(McServerRequestContext&& ctx, McRequestWithMcOp<op>&& req) {
    send(std::move(ctx),
         std::move(req),
         &McServerRequestContext::reply);
  }

  template <class ThriftType>
  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<ThriftType>&& req) {
    using Reply = ReplyT<TypedThriftRequest<ThriftType>>;
    send(std::move(ctx),
         std::move(req),
         &McServerRequestContext::reply<Reply>);
  }

  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<cpp2::McVersionRequest>&&) {
    TypedThriftReply<cpp2::McVersionReply> reply(mc_res_ok);
    reply.setValue(MCROUTER_PACKAGE_STRING);

    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }

  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<cpp2::McQuitRequest>&&) {
    using Reply = TypedThriftReply<cpp2::McQuitReply>;
    McServerRequestContext::reply(std::move(ctx), Reply(mc_res_ok));
  }

  void onRequest(McServerRequestContext&& ctx,
                 TypedThriftRequest<cpp2::McShutdownRequest>&&) {
    using Reply = TypedThriftReply<cpp2::McShutdownReply>;
    McServerRequestContext::reply(std::move(ctx), Reply(mc_res_ok));
  }

  template <class Request>
  void send(McServerRequestContext&& ctx,
            Request&& req,
            ReplyFunction<Request> replyFn) {
    auto rctx = folly::make_unique<ServerRequestContext<Request>>(
        std::move(ctx),
        std::move(req));
    auto& reqRef = rctx->req;
    auto& sessionRef = rctx->ctx.session();

    auto cb = [ sctx = std::move(rctx), replyFn ](
        const Request&, ReplyT<Request>&& reply) {
      replyFn(std::move(sctx->ctx), std::move(reply));
    };

    if (retainSourceIp_) {
      auto peerIp = sessionRef.getSocketAddress().getAddressStr();
      client_.send(reqRef, std::move(cb), peerIp);
    } else {
      client_.send(reqRef, std::move(cb));
    }
  }

 private:
  McrouterClient& client_;
  bool retainSourceIp_{false};
};

}}} // facebook::memcache::mcrouter
