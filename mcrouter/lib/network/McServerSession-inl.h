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

#include <memory>

#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/MultiOpParent.h"
#include "mcrouter/lib/network/CarbonMessageList.h"

namespace facebook { namespace memcache {

template <class Request>
void McServerSession::asciiRequestReady(Request&& req,
                                        mc_res_t result,
                                        bool noreply) {
  DestructorGuard dg(this);

  using Reply = ReplyT<Request>;

  assert(parser_.protocol() == mc_ascii_protocol);
  assert(!parser_.outOfOrder());

  if (state_ != STREAMING) {
    return;
  }

  if (GetLike<Request>::value && !currentMultiop_) {
    currentMultiop_ = std::make_shared<MultiOpParent>(*this, tailReqid_++);
  }
  uint64_t reqid;
  reqid = tailReqid_++;

  /* We need op in order for the reply to be handled correctly */
  static constexpr mc_op_t op = OpFromType<Request, RequestOpMapping>::value;
  McServerRequestContext ctx(*this, op, reqid, noreply, currentMultiop_);

  ctx.asciiKey().emplace(req.key().raw().cloneOneAsValue());

  if (result == mc_res_bad_key) {
    McServerRequestContext::reply(std::move(ctx), Reply(mc_res_bad_key));
  } else {
    onRequest_->requestReady(std::move(ctx), std::move(req));
  }
}

template <class Request>
void McServerSession::umbrellaRequestReady(Request&& req, uint64_t reqid) {
  DestructorGuard dg(this);

  using Reply = ReplyT<Request>;

  assert(parser_.protocol() == mc_umbrella_protocol);
  assert(parser_.outOfOrder());

  if (state_ != STREAMING) {
    return;
  }

  /* We need op in order for the reply to be handled correctly */
  static constexpr mc_op_t op = OpFromType<Request, RequestOpMapping>::value;
  McServerRequestContext ctx(*this, op, reqid, false /* noreply */,
                             nullptr /* MultiOpParent */);

  if (ctx.operation_ == mc_op_version && options_.defaultVersionHandler) {
    // Handle version command only if the user doesn't want to handle it
    // themselves.
    McVersionReply versionReply(mc_res_ok);
    versionReply.value() =
        folly::IOBuf(folly::IOBuf::COPY_BUFFER, options_.versionString);
    McServerRequestContext::reply(std::move(ctx), std::move(versionReply));
  } else if (ctx.operation_ == mc_op_quit) {
    McServerRequestContext::reply(std::move(ctx), Reply(mc_res_ok));
    close();
  } else if (ctx.operation_ == mc_op_shutdown) {
    McServerRequestContext::reply(std::move(ctx), Reply(mc_res_ok));
    stateCb_.onShutdown();
  } else {
    onRequest_->requestReady(std::move(ctx), std::move(req));
  }
}

}} // facebook::memcache
