/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook {
namespace memcache {

template <class Request>
void McServerSession::asciiRequestReady(
    Request&& req,
    mc_res_t result,
    bool noreply) {
  DestructorGuard dg(this);

  using Reply = ReplyT<Request>;

  assert(parser_.protocol() == mc_ascii_protocol);
  assert(!parser_.outOfOrder());

  if (state_ != STREAMING) {
    return;
  }

  if (carbon::GetLike<Request>::value && !currentMultiop_) {
    currentMultiop_ = std::make_shared<MultiOpParent>(*this, tailReqid_++);
  }
  uint64_t reqid;
  reqid = tailReqid_++;

  McServerRequestContext ctx(*this, reqid, noreply, currentMultiop_);

  ctx.asciiKey().emplace(req.key().raw().cloneOneAsValue());

  if (result == mc_res_bad_key) {
    McServerRequestContext::reply(std::move(ctx), Reply(mc_res_bad_key));
  } else {
    try {
      onRequest_->requestReady(std::move(ctx), std::move(req));
    } catch (...) {
      McServerRequestContext::reply(std::move(ctx), Reply(mc_res_remote_error));
    }
  }
}

template <class Request>
void McServerSession::umbrellaRequestReady(Request&& req, uint64_t reqid) {
  DestructorGuard dg(this);

  assert(parser_.protocol() == mc_umbrella_protocol_DONOTUSE);
  assert(parser_.outOfOrder());

  if (state_ != STREAMING) {
    return;
  }

  McServerRequestContext ctx(*this, reqid);

  umbrellaRequestReadyImpl(std::move(ctx), std::move(req));
}

template <class Request>
void McServerSession::umbrellaRequestReadyImpl(
    McServerRequestContext&& ctx,
    Request&& req) {
  onRequest_->requestReady(std::move(ctx), std::move(req));
}

template <>
inline void McServerSession::umbrellaRequestReadyImpl(
    McServerRequestContext&& ctx,
    McVersionRequest&& req) {
  if (options_.defaultVersionHandler) {
    McVersionReply versionReply(mc_res_ok);
    versionReply.value() =
        folly::IOBuf(folly::IOBuf::COPY_BUFFER, options_.versionString);
    McServerRequestContext::reply(std::move(ctx), std::move(versionReply));
  } else {
    onRequest_->requestReady(std::move(ctx), std::move(req));
  }
}

template <>
inline void McServerSession::umbrellaRequestReadyImpl(
    McServerRequestContext&& ctx,
    McQuitRequest&& /* req */) {
  McServerRequestContext::reply(std::move(ctx), McQuitReply(mc_res_ok));
  close();
}

template <>
inline void McServerSession::umbrellaRequestReadyImpl(
    McServerRequestContext&& ctx,
    McShutdownRequest&& /* req */) {
  McServerRequestContext::reply(std::move(ctx), McShutdownReply(mc_res_ok));
  stateCb_.onShutdown();
}
}
} // facebook::memcache
