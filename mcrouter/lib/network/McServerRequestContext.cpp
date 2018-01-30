/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

namespace facebook {
namespace memcache {

McServerSession& McServerRequestContext::session() {
  assert(session_ != nullptr);
  return *session_;
}

McServerRequestContext::McServerRequestContext(
    McServerSession& s,
    uint64_t r,
    bool nr,
    std::shared_ptr<MultiOpParent> parent,
    bool isEndContext)
    : session_(&s), isEndContext_(isEndContext), noReply_(nr), reqid_(r) {
  if (parent) {
    asciiState_ = std::make_unique<AsciiState>();
    asciiState_->parent_ = std::move(parent);
    asciiState_->parent_->recordRequest();
  }

  session_->onTransactionStarted(hasParent() || isEndContext_);
}

McServerRequestContext::McServerRequestContext(
    McServerRequestContext&& other) noexcept
    : session_(other.session_),
      isEndContext_(other.isEndContext_),
      noReply_(other.noReply_),
      replied_(other.replied_),
      reqid_(other.reqid_),
      asciiState_(std::move(other.asciiState_)) {
  other.session_ = nullptr;
}

McServerRequestContext& McServerRequestContext::operator=(
    McServerRequestContext&& other) {
  session_ = other.session_;
  isEndContext_ = other.isEndContext_;
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
    session_->onTransactionCompleted(hasParent() || isEndContext_);
  }
}

// Note: defined in .cpp in order to avoid circular dependency between
// McServerRequestContext.h and MultiOpParent.h.
bool McServerRequestContext::moveReplyToParent(
    mc_res_t result,
    uint32_t errorCode,
    std::string&& errorMessage) const {
  return hasParent() &&
      parent().reply(result, errorCode, std::move(errorMessage));
}

// Also defined in .cpp to avoid the same circular dependency
bool McServerRequestContext::isParentError() const {
  return parent().error();
}

double McServerRequestContext::getDropProbability() const {
  if (session_ == nullptr) {
    return 0.0;
  }

  double dropProbability = 0.0;

  if (session_->getCpuController()) {
    dropProbability = session_->getCpuController()->getDropProbability();
  }

  if (session_->getMemController()) {
    dropProbability = std::max(
        dropProbability, session_->getMemController()->getDropProbability());
  }

  return dropProbability;
}

ServerLoad McServerRequestContext::getServerLoad() const noexcept {
  if (session_) {
    if (const auto& cpuController = session_->getCpuController()) {
      return cpuController->getServerLoad();
    }
  }
  return ServerLoad::zero();
}

} // memcache
} // facebook
