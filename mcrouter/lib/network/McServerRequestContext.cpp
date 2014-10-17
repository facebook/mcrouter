/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McServerRequestContext.h"

#include "mcrouter/lib/network/McServerSession.h"
#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook { namespace memcache {

void McServerRequestContext::reply(
  McServerRequestContext&& ctx,
  McReply&& reply) {

  auto session = ctx.session_;

  ctx.replied_ = true;

  if (ctx.parent_ && ctx.parent_->reply(std::move(reply))) {
    /* parent stole the reply */
    session->reply(std::move(ctx), McReply());
  } else {
    session->reply(std::move(ctx), std::move(reply));
  }
}

bool McServerRequestContext::noReply(const McReply& reply) const {
  if (noReply_) {
    return true;
  }

  if (!parent_) {
    return false;
  }

  /* No reply if either:
     1) We saw an error (the error will be printed out by the end context)
     2) This is a miss, except for lease_get (lease get misses still have
     'LVALUE' replies with the token) */
  return (parent_->error() ||
          !(reply.result() == mc_res_found ||
            operation_ == mc_op_lease_get));
}

McServerRequestContext::McServerRequestContext(
  McServerSession& s, mc_op_t op, uint64_t r, bool nr,
  std::shared_ptr<MultiOpParent> parent)
    : session_(&s),
      operation_(op),
      reqid_(r),
      noReply_(nr),
      parent_(std::move(parent)) {

  if (parent_) {
    parent_->recordRequest();
  }

  session_->onTransactionStarted(parent_ != nullptr ||
                                 operation_ == mc_op_end);
}

McServerRequestContext::McServerRequestContext(
  McServerRequestContext&& other) noexcept
    : session_(other.session_),
      operation_(other.operation_),
      reqid_(other.reqid_),
      noReply_(other.noReply_),
      replied_(other.replied_),
      parent_(std::move(other.parent_)),
      key_(std::move(other.key_)) {
  other.session_ = nullptr;
}

McServerRequestContext& McServerRequestContext::operator=(
  McServerRequestContext&& other) {

  session_ = other.session_;
  operation_ = other.operation_;
  reqid_ = other.reqid_;
  noReply_ = other.noReply_;
  replied_ = other.replied_;
  parent_ = std::move(other.parent_);
  key_ = std::move(other.key_);
  other.session_ = nullptr;

  return *this;
}

McServerRequestContext::~McServerRequestContext() {
  if (session_) {
    /* Check that a reply was returned */
    assert(replied_);
    session_->onTransactionCompleted(parent_ != nullptr ||
                                     operation_ == mc_op_end);
  }
}

}}  // facebook::memcache
