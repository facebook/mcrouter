/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McClientRequestContext.h"

namespace facebook { namespace memcache {

void McClientRequestContextBase::canceled() {
  state_ = ReqState::NONE;
  baton_.post();
}

McClientRequestContextBase::~McClientRequestContextBase() {
  assert(state_ == ReqState::NONE || state_ == ReqState::COMPLETE);
}

McClientRequestContextQueue::McClientRequestContextQueue(
  bool outOfOrder) noexcept : outOfOrder_(outOfOrder) {
}

size_t McClientRequestContextQueue::getPendingRequestCount() const noexcept {
  return pendingQueue_.size();
}

size_t McClientRequestContextQueue::getInflightRequestCount() const noexcept {
  return writeQueue_.size() + pendingReplyQueue_.size();
}

bool McClientRequestContextQueue::hasPendingReply() const noexcept {
  return !pendingReplyQueue_.empty();
}

McClientRequestContextBase&
McClientRequestContextQueue::getFirstPendingReply() {
  return pendingReplyQueue_.front();
}

void McClientRequestContextQueue::failAllSent(mc_res_t error) {
  failQueue(pendingReplyQueue_, error);
}

void McClientRequestContextQueue::failAllPending(mc_res_t error) {
  assert(pendingReplyQueue_.empty());
  assert(writeQueue_.empty());
  failQueue(pendingQueue_, error);
}

void McClientRequestContextQueue::clearStoredInitializers() {
  while (!initializers_.empty()) {
    initializers_.pop();
  }
}

size_t McClientRequestContextQueue::getFirstId() const {
  assert(getPendingRequestCount());
  return pendingQueue_.front().id;
}

void McClientRequestContextQueue::markAsPending(
    McClientRequestContextBase& req) {
  assert(req.state_ == State::NONE);
  req.state_ = State::PENDING_QUEUE;
  pendingQueue_.push_back(req);

  if (outOfOrder_) {
    idMap_[req.id] = &req;
  }
}

McClientRequestContextBase& McClientRequestContextQueue::markNextAsSending() {
  auto& req = pendingQueue_.front();
  pendingQueue_.pop_front();
  assert(req.state_ == State::PENDING_QUEUE);
  req.state_ = State::WRITE_QUEUE;
  writeQueue_.push_back(req);
  return req;
}

McClientRequestContextBase& McClientRequestContextQueue::markNextAsSent() {
  auto& req = writeQueue_.front();
  writeQueue_.pop_front();
  if (req.state_ == State::WRITE_QUEUE_CANCELED) {
    removeFromMap(req.id);
    req.canceled();
  } else {
    assert(req.state_ == State::WRITE_QUEUE);
    req.state_ = State::PENDING_REPLY_QUEUE;
    pendingReplyQueue_.push_back(req);
  }
  return req;
}

void McClientRequestContextQueue::failQueue(
    McClientRequestContextBase::Queue& queue, mc_res_t error) {
  while (!queue.empty()) {
    auto& req = queue.front();
    queue.pop_front();
    removeFromMap(req.id);
    req.state_ = State::NONE;
    req.replyError(error);
  }
}

void McClientRequestContextQueue::removeFromMap(uint64_t id) {
  if (outOfOrder_) {
    idMap_.erase(id);
  }
}

void McClientRequestContextQueue::removePending(
    McClientRequestContextBase& req) {
  assert(req.state_ == State::PENDING_QUEUE);
  removeFromMap(req.id);
  pendingQueue_.erase(pendingQueue_.iterator_to(req));
  req.state_ = State::NONE;
}

void McClientRequestContextQueue::removePendingReply(
    McClientRequestContextBase& req) {
  assert(req.state_ == State::PENDING_REPLY_QUEUE);
  assert(&req == &pendingReplyQueue_.front());
  removeFromMap(req.id);
  pendingReplyQueue_.erase(pendingReplyQueue_.iterator_to(req));
  req.state_ = State::NONE;
  initializers_.push(req.initializer_);
}

McClientRequestContextBase::InitializerFuncPtr
McClientRequestContextQueue::getParserInitializer(uint64_t reqId) {
  if (outOfOrder_) {
    auto it = idMap_.find(reqId);
    if (it != idMap_.end()) {
      return it->second->initializer_;
    }
  } else {
    // In inorder protocol we expect to receive timedout requests first.
    if (!initializers_.empty()) {
      return initializers_.front();
    } else if (!pendingReplyQueue_.empty()) {
      return pendingReplyQueue_.front().initializer_;
    }
  }
  return nullptr;
}

}}  // facebook::memcache
