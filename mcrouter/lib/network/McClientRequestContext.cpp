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

constexpr size_t kSerializedRequestContextLength = 1024;

void McClientRequestContextBase::replyError(mc_res_t result) {
  assert(state_ == ReqState::NONE);
  replyErrorImpl(result);
  state_ = ReqState::COMPLETE;
  baton_.post();
}

void McClientRequestContextBase::canceled() {
  state_ = ReqState::NONE;
  baton_.post();
}

McClientRequestContextBase::~McClientRequestContextBase() {
  assert(state_ == ReqState::NONE || state_ == ReqState::COMPLETE);
}

McClientRequestContextQueue::McClientRequestContextQueue(
  bool outOfOrder) noexcept
  : outOfOrder_(outOfOrder),
    buckets_(kDefaultNumBuckets),
    set_(McClientRequestContextBase::UnorderedSet::bucket_traits(
      buckets_.data(), buckets_.size())) {
}

void McClientRequestContextQueue::growBucketsArray() {
  // Allocate buckets array that is twice bigger than the current one.
  std::vector<McClientRequestContextBase::UnorderedSet::bucket_type>
  tmp(buckets_.size() * 2);
  set_.rehash(McClientRequestContextBase::UnorderedSet::bucket_traits(
    tmp.data(), tmp.size()));
  // Use swap here, since it has better defined behaviour regarding iterators.
  buckets_.swap(tmp);
}

size_t McClientRequestContextQueue::getPendingRequestCount() const noexcept {
  return pendingQueue_.size();
}

size_t McClientRequestContextQueue::getInflightRequestCount() const noexcept {
  return repliedQueue_.size() + writeQueue_.size() + pendingReplyQueue_.size();
}

void McClientRequestContextQueue::failAllSent(mc_res_t error) {
  clearStoredInitializers();
  failQueue(pendingReplyQueue_, error);
}

void McClientRequestContextQueue::failAllPending(mc_res_t error) {
  assert(pendingReplyQueue_.empty());
  assert(writeQueue_.empty());
  assert(repliedQueue_.empty());
  failQueue(pendingQueue_, error);
}

void McClientRequestContextQueue::clearStoredInitializers() {
  while (!timedOutInitializers_.empty()) {
    timedOutInitializers_.pop();
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
    // We hit the number of allocated buckets, grow the array.
    if (set_.size() >= buckets_.size()) {
      growBucketsArray();
    }
    set_.insert(req);
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
  if (!repliedQueue_.empty()) {
    auto& req = repliedQueue_.front();
    repliedQueue_.pop_front();
    req.state_ = State::COMPLETE;
    req.baton_.post();
    return req;
  }

  auto& req = writeQueue_.front();
  writeQueue_.pop_front();
  if (req.state_ == State::WRITE_QUEUE_CANCELED) {
    removeFromSet(req);
    // We already sent this request, so we're going to get a reply in future.
    if (!outOfOrder_) {
      timedOutInitializers_.push(req.initializer_);
    }
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
    removeFromSet(req);
    req.state_ = State::NONE;
    req.replyError(error);
  }
}

McClientRequestContextBase::UnorderedSet::iterator
McClientRequestContextQueue::getContextById(uint64_t id) {
  return
    set_.find(
      id,
      std::hash<uint64_t>(),
      [] (uint64_t id, const McClientRequestContextBase& ctx) {
        return id == ctx.id;
      });
}

void McClientRequestContextQueue::removeFromSet(
    McClientRequestContextBase& req) {
  if (outOfOrder_) {
    set_.erase(req);
  }
}

void McClientRequestContextQueue::removePending(
    McClientRequestContextBase& req) {
  assert(req.state_ == State::PENDING_QUEUE);
  removeFromSet(req);
  pendingQueue_.erase(pendingQueue_.iterator_to(req));
  req.state_ = State::NONE;
}

void McClientRequestContextQueue::removePendingReply(
    McClientRequestContextBase& req) {
  assert(req.state_ == State::PENDING_REPLY_QUEUE);
  assert(&req == &pendingReplyQueue_.front() || outOfOrder_);
  removeFromSet(req);
  pendingReplyQueue_.erase(pendingReplyQueue_.iterator_to(req));
  req.state_ = State::NONE;
  // We need timedOutInitializers_ only for in order protocol.
  if (!outOfOrder_) {
    timedOutInitializers_.push(req.initializer_);
  }
}

McClientRequestContextBase::InitializerFuncPtr
McClientRequestContextQueue::getParserInitializer(uint64_t reqId) {
  if (outOfOrder_) {
    auto it = getContextById(reqId);
    if (it != set_.end()) {
      return it->initializer_;
    }
  } else {
    // In inorder protocol we expect to receive timedout requests first.
    if (!timedOutInitializers_.empty()) {
      return timedOutInitializers_.front();
    } else if (!pendingReplyQueue_.empty()) {
      return pendingReplyQueue_.front().initializer_;
    } else if (!writeQueue_.empty()) {
      return writeQueue_.front().initializer_;
    }
  }
  return nullptr;
}

std::string McClientRequestContextQueue::debugInfo() const {
  return folly::sformat(
    "Currently have {} timedout initializers, {} requests in "
    "replied queue, {} requests in pending reply queue, "
    "{} requests in write queue and {} requests in pending queue, "
    "the first alive request is: {}", timedOutInitializers_.size(),
    repliedQueue_.size(), pendingReplyQueue_.size(), writeQueue_.size(),
    pendingQueue_.size(), getFirstAliveRequestInfo());
}

std::string McClientRequestContextQueue::getFirstAliveRequestInfo() const {
  const McClientRequestContextBase* ctx{nullptr};

  if (!pendingReplyQueue_.empty()) {
    ctx = &pendingReplyQueue_.front();
  } else if (!writeQueue_.empty()) {
    ctx = &writeQueue_.front();
  } else {
    return "no alive requests that were sent or are being sent";
  }

  size_t dataLen = 0;
  for (size_t i = 0;
       i < ctx->reqContext.getIovsCount(); ++i) {
    dataLen += ctx->reqContext.getIovs()[i].iov_len;
  }
  dataLen = std::min(dataLen, kSerializedRequestContextLength);
  std::vector<char> data(dataLen);

  for (size_t i = 0, dataOffset = 0;
       i < ctx->reqContext.getIovsCount() &&
       dataOffset < dataLen; ++i) {
    auto toCopy =
        std::min(dataLen - dataOffset, ctx->reqContext.getIovs()[i].iov_len);
    memcpy(data.data() + dataOffset, ctx->reqContext.getIovs()[i].iov_base,
           toCopy);
    dataOffset += toCopy;
  }

  return folly::sformat("{}, serialized data was: \"{}\"",
                        ctx->getContextTypeStr(),
                        folly::cEscape<std::string>(
                            folly::StringPiece(data.data(), data.size())));
}

}}  // facebook::memcache
