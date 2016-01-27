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
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/McrouterClient.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class Request>
struct ServerRequestContext {
  McServerRequestContext ctx;
  Request req;

  ServerRequestContext(McServerRequestContext&& ctx_, Request&& req_)
      : ctx(std::move(ctx_)), req(std::move(req_)) {}
};

using ReplyFunction = void (*)(McServerRequestContext&& ctx, McReply&& reply);

class ServerOnRequestCommon {
 public:
  ServerOnRequestCommon(McrouterClient& client, bool retainSourceIp)
      : client_(client), retainSourceIp_(retainSourceIp) {}

  void onRequest(McServerRequestContext&& ctx,
                 McRequestWithMcOp<mc_op_version>&& req) {
    McServerRequestContext::reply(std::move(ctx),
                                  McReply(mc_res_ok, MCROUTER_PACKAGE_STRING));
  }

  template <class Request>
  void onRequest(McServerRequestContext&& ctx,
                 Request&& req) {
    send(std::move(ctx),
         std::move(req),
         &McServerRequestContext::reply);
  }

  template <class Request>
  void send(McServerRequestContext&& ctx,
            Request&& req,
            ReplyFunction replyFn) {

    auto rctx = folly::make_unique<ServerRequestContext<Request>>(
        std::move(ctx),
        std::move(req));
    auto& reqRef = rctx->req;
    auto& sessionRef = rctx->ctx.session();

    auto cb = [sctx = std::move(rctx), replyFn](McReply&& reply) {
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
} // mcrouter
} // memcache
} // facebook
