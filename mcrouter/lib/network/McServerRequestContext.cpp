/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McServerRequestContext.h"

#include "mcrouter/lib/network/McServerSession.h"
#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook { namespace memcache {

McServerSession& McServerRequestContext::session() {
  assert(session_ != nullptr);
  return *session_;
}

void McServerRequestContext::reply(
  McServerRequestContext&& ctx,
  McReply&& reply) {

  ctx.replied_ = true;

  if (ctx.hasParent() && ctx.parent().reply(std::move(reply))) {
    /* parent stole the reply */
    replyImpl(std::move(ctx), McReply());
  } else {
    replyImpl(std::move(ctx), std::move(reply));
  }
}

void McServerRequestContext::replyImpl(McServerRequestContext&& ctx,
                                       McReply&& reply) {

  auto session = ctx.session_;

  if (ctx.noReply(reply)) {
    session->reply(nullptr, ctx.reqid_);
    return;
  }

  if (!session->ensureWriteBufs()) {
    return;
  }

  uint64_t reqid = ctx.reqid_;
  auto wb = session->writeBufs_->get();
  if (!wb->prepare(std::move(ctx), std::move(reply))) {
    session->transport_->close();
    return;
  }
  session->reply(std::move(wb), reqid);
}

bool McServerRequestContext::noReply(const McReply& reply) const {
  if (noReply_) {
    return true;
  }

  if (!hasParent()) {
    return false;
  }

  /* No reply if either:
     1) We saw an error (the error will be printed out by the end context)
     2) This is a miss, except for lease_get (lease get misses still have
     'LVALUE' replies with the token) */
  return (parent().error() ||
          !(reply.result() == mc_res_found ||
            operation_ == mc_op_lease_get));
}

McServerRequestContext::McServerRequestContext(
  McServerSession& s, mc_op_t op, uint64_t r, bool nr,
  std::shared_ptr<MultiOpParent> parent)
    : session_(&s),
      operation_(op),
      noReply_(nr),
      reqid_(r) {

  if (parent) {
    asciiState_ = folly::make_unique<AsciiState>();
    asciiState_->parent_ = std::move(parent);
    asciiState_->parent_->recordRequest();
  }

  session_->onTransactionStarted(hasParent() || operation_ == mc_op_end);
}

McServerRequestContext::McServerRequestContext(
    McServerRequestContext&& other) noexcept
    : session_(other.session_),
      operation_(other.operation_),
      noReply_(other.noReply_),
      replied_(other.replied_),
      reqid_(other.reqid_),
      asciiState_(std::move(other.asciiState_)) {
  other.session_ = nullptr;
}

McServerRequestContext& McServerRequestContext::operator=(
  McServerRequestContext&& other) {

  session_ = other.session_;
  operation_ = other.operation_;
  reqid_ = other.reqid_;
  noReply_ = other.noReply_;
  replied_ = other.replied_;
  asciiState_ = std::move(other.asciiState_);
  other.session_ = nullptr;

  return *this;
}

McServerRequestContext::~McServerRequestContext() {
  if (session_) {
    /* Check that a reply was returned */
    assert(replied_);
    session_->onTransactionCompleted(hasParent() || operation_ == mc_op_end);
  }
}

}}  // facebook::memcache
