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

#include "folly/Memory.h"
#include "mcrouter/lib/network/McServerTransaction.h"

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

std::shared_ptr<McServerSession> McServerSession::create(
  apache::thrift::async::TAsyncTransport::UniquePtr transport,
  std::shared_ptr<McServerOnRequest> cb,
  std::function<void(std::shared_ptr<McServerSession>)> onClosed,
  std::function<void()> onShutdown,
  AsyncMcServerWorker::Options options) {

  auto ptr = std::shared_ptr<McServerSession>(
    new McServerSession(
      std::move(transport),
      std::move(cb),
      std::move(onClosed),
      std::move(onShutdown),
      std::move(options)
    ),
    Destructor());

  ptr->weakThis_ = std::weak_ptr<McServerSession>(ptr);
  return ptr;
}

McServerSession::McServerSession(
  apache::thrift::async::TAsyncTransport::UniquePtr transport,
  std::shared_ptr<McServerOnRequest> cb,
  std::function<void(std::shared_ptr<McServerSession>)> onClosed,
  std::function<void()> onShutdown,
  AsyncMcServerWorker::Options options)
    : transport_(std::move(transport)),
      onRequest_(std::move(cb)),
      onClosed_(std::move(onClosed)),
      onShutdown_(std::move(onShutdown)),
      options_(std::move(options)),
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
    assert(unansweredRequests_.empty());
    assert(multigetRequests_.empty());
    assert(pendingWrites_.empty());

    if (state_ == CLOSING) {
      transport_->close();
      if (onClosed_) {
        /* Ok if nullptr */
        onClosed_(weakThis_.lock());
      }
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

  checkClosed();
  if (options_.maxInFlight > 0 &&
      realRequestsInFlight_ < options_.maxInFlight) {
    resume(PAUSE_THROTTLED);
  }
}

void McServerSession::onRequestReplied(McServerTransaction& transaction) {

  DestructorGuard dg(this);

  if (parser_.outOfOrder()) {
    queueWrite(unansweredRequests_.extract(
      unansweredRequests_.iterator_to(transaction)));
  } else {
    while (!unansweredRequests_.empty() &&
           unansweredRequests_.front().replyReady()) {
      queueWrite(unansweredRequests_.popFront());
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

void McServerSession::requestReady(McRequest req,
                                   mc_op_t operation,
                                   uint64_t reqid,
                                   mc_res_t result,
                                   bool noreply) {
  DestructorGuard dg(this);

  auto sharedThis = weakThis_.lock();
  if (!sharedThis) {
    /* This session is being destroyed, can't create new transactions */
    close();
    return;
  }

  if (state_ != STREAMING) {
    return;
  }

  auto isSubRequest = isPartOfMultiget(parser_.protocol(), operation);
  auto isMultiget = (operation == mc_op_end);
  auto transactionPtr =
    folly::make_unique<McServerTransaction>(
      sharedThis,
      std::move(req),
      operation,
      reqid,
      isMultiget,
      isSubRequest,
      noreply);
  McServerTransaction& transaction = [&]() -> McServerTransaction& {
    if (isSubRequest) {
      return multigetRequests_.pushBack(std::move(transactionPtr));
    } else {
      return unansweredRequests_.pushBack(std::move(transactionPtr));
    }
  }();

  if (result == mc_res_bad_key) {
    transaction.sendReply(McReply(mc_res_bad_key));
  } else if (operation == mc_op_version) {
    transaction.sendReply(McReply(mc_res_ok, options_.versionString));
  } else if (operation == mc_op_quit) {
    /* mc_op_quit transaction will have `noreply` set, so this call
       is solely to make sure the transaction is completed and cleaned up */
    transaction.sendReply(McReply(mc_res_ok));
    close();
  } else if (operation == mc_op_shutdown) {
    transaction.sendReply(McReply(mc_res_ok));
    onShutdown_();
  } else if (isMultiget) {
    while (!multigetRequests_.empty()) {
      auto subReq = multigetRequests_.popFront();
      transaction.pushMultigetRequest(std::move(subReq));
    }
    transaction.dispatchSubRequests(*onRequest_);
  } else if (!isSubRequest) {
    transaction.dispatchRequest(*onRequest_);
  }
}

void McServerSession::parseError(McReply reply) {
  DestructorGuard dg(this);

  auto sharedThis = weakThis_.lock();
  if (!sharedThis) {
    /* This session is being destroyed, can't create new transactions */
    close();
    return;
  }

  auto& transaction = unansweredRequests_.pushBack(
    folly::make_unique<McServerTransaction>(
      sharedThis,
      McRequest(createMcMsgRef()),
      mc_op_unknown,
      0)
  );

  transaction.sendReply(std::move(reply));
  close();
}

void McServerSession::queueWrite(
  std::unique_ptr<McServerTransaction> ptransaction) {
  DestructorGuard dg(this);

  if (ptransaction->noReply()) {
    return;
  }

  /* TODO: the subrequests should simply add to the parent's request
     iovs, so that we only write once */
  ptransaction->queueSubRequestsWrites();
  auto& transaction = pendingWrites_.pushBack(std::move(ptransaction));
  if (options_.singleWrite) {
    if (!transaction.prepareWrite()) {
      transport_->close();
      return;
    }

    transport_->writev(this, transaction.iovs_, transaction.niovs_);
    if (!pendingWrites_.empty()) {
      /* We only need to pause if the sendmsg() call didn't write everything
         in one go */
      pause(PAUSE_WRITE);
    }
  } else {
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

  writeBatches_.emplace_back();
  auto& batch = writeBatches_.back();
  std::vector<struct iovec> iovs;
  while (!pendingWrites_.empty()) {
    auto& transaction = batch.pushBack(pendingWrites_.popFront());
    if (!transaction.prepareWrite()) {
      transport_->close();
      return;
    }

    iovs.insert(iovs.end(), transaction.iovs_,
                transaction.iovs_ + transaction.niovs_);
  }

  transport_->writev(this, iovs.data(), iovs.size());
}

void McServerSession::completeWrite() {
  if (options_.singleWrite) {
    assert(!pendingWrites_.empty());
    pendingWrites_.popFront();
  } else {
    assert(!writeBatches_.empty());
    writeBatches_.pop_front();
  }
}

void McServerSession::writeSuccess() noexcept {
  DestructorGuard dg(this);
  completeWrite();

  /* No-op if not paused */
  resume(PAUSE_WRITE);
}

void McServerSession::writeError(
  size_t bytesWritten,
  const apache::thrift::transport::TTransportException& ex) noexcept {

  DestructorGuard dg(this);
  completeWrite();
}

}}  // facebook::memcache
