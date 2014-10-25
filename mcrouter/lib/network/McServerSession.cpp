/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McServerSession.h"

#include <memory>

#include <folly/Memory.h>

#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook { namespace memcache {

namespace {

/**
 * @return true  If this incoming request is a part of a multiget request.
 */
bool isPartOfMultiget(mc_protocol_t protocol, mc_op_t operation) {
  if (protocol != mc_ascii_protocol) {
    return false;
  }

  if (operation == mc_op_get ||
      operation == mc_op_gets ||
      operation == mc_op_lease_get ||
      operation == mc_op_metaget) {
    return true;
  }

  return false;
}

}  // namespace

McServerSession& McServerSession::create(
  apache::thrift::async::TAsyncTransport::UniquePtr transport,
  std::shared_ptr<McServerOnRequest> cb,
  std::function<void(McServerSession&)> onWriteSuccess,
  std::function<void(McServerSession&)> onTerminated,
  std::function<void()> onShutdown,
  AsyncMcServerWorkerOptions options,
  void* userCtxt) {

  auto ptr = new McServerSession(
    std::move(transport),
    std::move(cb),
    std::move(onWriteSuccess),
    std::move(onTerminated),
    std::move(onShutdown),
    std::move(options),
    userCtxt
  );

  return *ptr;
}

McServerSession::McServerSession(
  apache::thrift::async::TAsyncTransport::UniquePtr transport,
  std::shared_ptr<McServerOnRequest> cb,
  std::function<void(McServerSession&)> onWriteSuccess,
  std::function<void(McServerSession&)> onTerminated,
  std::function<void()> onShutdown,
  AsyncMcServerWorkerOptions options,
  void* userCtxt)
    : transport_(std::move(transport)),
      onRequest_(std::move(cb)),
      onWriteSuccess_(std::move(onWriteSuccess)),
      onTerminated_(std::move(onTerminated)),
      onShutdown_(std::move(onShutdown)),
      options_(std::move(options)),
      userCtxt_(userCtxt),
      parser_(this,
              options_.requestsPerRead,
              options_.minBufferSize,
              options_.maxBufferSize),
      sendWritesCallback_(*this) {

  transport_->setReadCallback(this);
}

void McServerSession::pause(PauseReason reason) {
  pauseState_ |= static_cast<uint64_t>(reason);

  transport_->setReadCallback(nullptr);
}

void McServerSession::resume(PauseReason reason) {
  pauseState_ &= ~static_cast<uint64_t>(reason);

  /* Client can half close the socket and in those cases there is
     no point in enabling reads */
  if (!pauseState_ &&
      state_ == STREAMING &&
      transport_->good()) {
    transport_->setReadCallback(this);
  }
}

void McServerSession::onTransactionStarted(bool isSubRequest) {
  DestructorGuard dg(this);

  ++inFlight_;
  if (!isSubRequest) {
    ++realRequestsInFlight_;
  }

  if (options_.maxInFlight > 0 &&
      realRequestsInFlight_ >= options_.maxInFlight) {
    pause(PAUSE_THROTTLED);
  }
}

void McServerSession::checkClosed() {
  if (!inFlight_) {
    assert(pendingWrites_.empty());

    if (state_ == CLOSING) {
      /* prevent readEOF() from being called */
      transport_->setReadCallback(nullptr);
      transport_.reset();
      if (onTerminated_) {
        onTerminated_(*this);
      }
      destroy();
    }
  }
}

void McServerSession::onTransactionCompleted(bool isSubRequest) {
  DestructorGuard dg(this);

  assert(inFlight_ > 0);
  --inFlight_;
  if (!isSubRequest) {
    assert(realRequestsInFlight_ > 0);
    --realRequestsInFlight_;
  }

  if (options_.maxInFlight > 0 &&
      realRequestsInFlight_ < options_.maxInFlight) {
    resume(PAUSE_THROTTLED);
  }

  checkClosed();
}

void McServerSession::reply(McServerRequestContext&& ctx, McReply&& reply) {
  DestructorGuard dg(this);

  if (parser_.outOfOrder()) {
    queueWrite(std::move(ctx), std::move(reply));
  } else {
    auto reqid = ctx.reqid_;
    if (reqid == headReqid_) {
      /* head of line reply, write it and all contiguous blocked replies */
      queueWrite(std::move(ctx), std::move(reply));
      auto it = blockedReplies_.find(++headReqid_);
      while (it != blockedReplies_.end()) {
        queueWrite(std::move(it->second.first), std::move(it->second.second));
        blockedReplies_.erase(it);
        it = blockedReplies_.find(++headReqid_);
      }
    } else {
      /* can't write this reply now, save for later */
      blockedReplies_.emplace(
        reqid,
        std::make_pair(std::move(ctx), std::move(reply)));
    }
  }
}

void McServerSession::close() {
  state_ = CLOSING;
  checkClosed();
}

void McServerSession::getReadBuffer(void** bufReturn, size_t* lenReturn) {
  auto buf = parser_.getReadBuffer();
  *bufReturn = buf.first;
  *lenReturn = buf.second;
}

void McServerSession::readDataAvailable(size_t len) noexcept {
  DestructorGuard dg(this);

  if (!parser_.readDataAvailable(len)) {
    close();
  }
}

void McServerSession::readEOF() noexcept {
  DestructorGuard dg(this);

  close();
}

void McServerSession::readError(
  const apache::thrift::transport::TTransportException& ex) noexcept {
  DestructorGuard dg(this);

  close();
}

void McServerSession::requestReady(McRequest&& req,
                                   mc_op_t operation,
                                   uint64_t reqid,
                                   mc_res_t result,
                                   bool noreply) {
  DestructorGuard dg(this);

  if (state_ != STREAMING) {
    return;
  }

  if (!parser_.outOfOrder()) {
    if (isPartOfMultiget(parser_.protocol(), operation) &&
        !currentMultiop_) {
      currentMultiop_ = std::make_shared<MultiOpParent>(*this, tailReqid_++);
    }

    reqid = tailReqid_++;

    if (operation == mc_op_end) {
      currentMultiop_->recordEnd(reqid);
      currentMultiop_.reset();
      return;
    }
  }

  McServerRequestContext ctx(*this, operation, reqid, noreply, currentMultiop_);

  if (parser_.protocol() == mc_ascii_protocol) {
    ctx.key_.emplace();
    req.key().cloneOneInto(ctx.key_.value());
  }

  if (result == mc_res_bad_key) {
    McServerRequestContext::reply(std::move(ctx), McReply(mc_res_bad_key));
  } else if (ctx.operation_ == mc_op_version) {
    McServerRequestContext::reply(std::move(ctx),
                                  McReply(mc_res_ok, options_.versionString));
  } else if (ctx.operation_ == mc_op_quit) {
    /* mc_op_quit transaction will have `noreply` set, so this call
       is solely to make sure the transaction is completed and cleaned up */
    McServerRequestContext::reply(std::move(ctx), McReply(mc_res_ok));
    close();
  } else if (ctx.operation_ == mc_op_shutdown) {
    McServerRequestContext::reply(std::move(ctx), McReply(mc_res_ok));
    onShutdown_();
  } else {
    onRequest_->requestReady(std::move(ctx), std::move(req), ctx.operation_);
  }
}

void McServerSession::parseError(McReply reply) {
  DestructorGuard dg(this);

  if (state_ != STREAMING) {
    return;
  }

  McServerRequestContext::reply(
    McServerRequestContext(*this, mc_op_unknown, tailReqid_++),
    std::move(reply));
  close();
}

void McServerSession::queueWrite(McServerRequestContext&& ctx,
                                 McReply&& reply) {
  DestructorGuard dg(this);

  if (ctx.noReply(reply)) {
    return;
  }

  if (options_.singleWrite) {
    struct iovec* i;
    size_t n;
    auto& wb = writeBufs_.push();
    if (!wb.prepare(std::move(ctx), std::move(reply),
                    parser_.protocol(), i, n)) {
      transport_->close();
      return;
    }
    transport_->writev(this, i, n);
    if (!writeBufs_.empty()) {
      /* We only need to pause if the sendmsg() call didn't write everything
         in one go */
      pause(PAUSE_WRITE);
    }
  } else {
    pendingWrites_.emplace_back(std::move(ctx), std::move(reply));

    if (!writeScheduled_) {
      auto eventBase = transport_->getEventBase();
      CHECK(eventBase != nullptr);
      eventBase->runInLoop(&sendWritesCallback_, /* thisIteration= */ true);
      writeScheduled_ = true;
    }
  }
}

void McServerSession::SendWritesCallback::runLoopCallback() noexcept {
  session_.sendWrites();
}

void McServerSession::sendWrites() {
  DestructorGuard dg(this);

  writeScheduled_ = false;

  std::vector<struct iovec> iovs;
  size_t count = 0;
  while (!pendingWrites_.empty()) {
    auto& pw = pendingWrites_.front();
    struct iovec* i;
    size_t n;
    auto& wb = writeBufs_.push();

    if (!wb.prepare(std::move(pw.first), std::move(pw.second),
                    parser_.protocol(), i, n)) {
      close();
      return;
    }
    pendingWrites_.pop_front();
    ++count;
    iovs.insert(iovs.end(), i, i + n);
  }
  writeBatches_.push_back(count);

  transport_->writev(this, iovs.data(), iovs.size());
}

void McServerSession::completeWrite() {
  size_t count;
  if (options_.singleWrite) {
    count = 1;
  } else {
    assert(!writeBatches_.empty());
    count = writeBatches_.front();
    writeBatches_.pop_front();
  }

  while (count-- > 0) {
    assert(!writeBufs_.empty());
    writeBufs_.pop();
  }
}

void McServerSession::writeSuccess() noexcept {
  DestructorGuard dg(this);
  completeWrite();

  if (onWriteSuccess_) {
    onWriteSuccess_(*this);
  }

  if (writeBufs_.empty()) {
    /* No-op if not paused */
    resume(PAUSE_WRITE);
  }
}

void McServerSession::writeError(
  size_t bytesWritten,
  const apache::thrift::transport::TTransportException& ex) noexcept {

  DestructorGuard dg(this);
  completeWrite();
}

}}  // facebook::memcache
