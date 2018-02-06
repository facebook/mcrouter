/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McServerSession.h"

#include <memory>

#include <folly/small_vector.h>

#include "mcrouter/lib/debug/FifoManager.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/McServerSSLUtil.h"
#include "mcrouter/lib/network/MultiOpParent.h"
#include "mcrouter/lib/network/WriteBuffer.h"

namespace facebook {
namespace memcache {

namespace {

ConnectionFifo getDebugFifo(
    const std::string& path,
    const folly::AsyncTransportWrapper* transport,
    const std::string& requestHandlerName) {
  if (!path.empty()) {
    if (auto fifoManager = FifoManager::getInstance()) {
      if (auto fifo = fifoManager->fetchThreadLocal(path)) {
        return ConnectionFifo(std::move(fifo), transport, requestHandlerName);
      }
    }
  }
  return ConnectionFifo();
}

} // anonymous namespace

constexpr size_t kIovecVectorSize = 64;

McServerSession& McServerSession::create(
    folly::AsyncTransportWrapper::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    StateCallback& stateCb,
    const AsyncMcServerWorkerOptions& options,
    void* userCtxt,
    const CompressionCodecMap* codecMap) {
  auto ptr = new McServerSession(
      std::move(transport),
      std::move(cb),
      stateCb,
      options,
      userCtxt,
      codecMap);

  assert(ptr->state_ == STREAMING);

  DestructorGuard dg(ptr);
  ptr->transport_->setReadCB(ptr);
  if (ptr->state_ != STREAMING) {
    throw std::runtime_error(
        "Failed to create McServerSession: setReadCB failed");
  }

  return *ptr;
}

McServerSession::McServerSession(
    folly::AsyncTransportWrapper::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    StateCallback& stateCb,
    const AsyncMcServerWorkerOptions& options,
    void* userCtxt,
    const CompressionCodecMap* codecMap)
    : options_(options),
      transport_(std::move(transport)),
      eventBase_(*transport_->getEventBase()),
      onRequest_(std::move(cb)),
      stateCb_(stateCb),
      debugFifo_(getDebugFifo(
          options_.debugFifoPath,
          transport_.get(),
          onRequest_->name())),
      pendingWrites_(std::make_unique<WriteBufferIntrusiveList>()),
      sendWritesCallback_(*this),
      compressionCodecMap_(codecMap),
      parser_(
          *this,
          options_.minBufferSize,
          options_.maxBufferSize,
          &debugFifo_),
      userCtxt_(userCtxt) {
  try {
    transport_->getPeerAddress(&socketAddress_);
  } catch (const std::exception& e) {
    // std::system_error or other exception, leave IP address empty
    LOG(WARNING) << "Failed to get socket address: " << e.what();
  }

  auto socket = transport_->getUnderlyingTransport<folly::AsyncSSLSocket>();
  if (socket != nullptr) {
    socket->sslAccept(this, /* timeout = */ std::chrono::milliseconds::zero());
  }
}

void McServerSession::pause(PauseReason reason) {
  pauseState_ |= static_cast<uint64_t>(reason);

  transport_->setReadCB(nullptr);
}

void McServerSession::resume(PauseReason reason) {
  pauseState_ &= ~static_cast<uint64_t>(reason);

  /* Client can half close the socket and in those cases there is
     no point in enabling reads */
  if (!pauseState_ && state_ == STREAMING && transport_->good()) {
    transport_->setReadCB(this);
  }
}

void McServerSession::onTransactionStarted(bool isSubRequest) {
  ++inFlight_;
  if (options_.maxInFlight > 0 && !isSubRequest) {
    if (++realRequestsInFlight_ >= options_.maxInFlight) {
      DestructorGuard dg(this);
      pause(PAUSE_THROTTLED);
    }
  }
}

void McServerSession::checkClosed() {
  if (!inFlight_) {
    assert(pendingWrites_->empty());

    if (state_ == CLOSING) {
      /* It's possible to call close() more than once from the same stack.
         Prevent second close() from doing anything */
      state_ = CLOSED;
      if (transport_) {
        /* prevent readEOF() from being called */
        transport_->setReadCB(nullptr);
        transport_.reset();
      }
      stateCb_.onCloseFinish(*this);
      destroy();
    }
  }
}

void McServerSession::onTransactionCompleted(bool isSubRequest) {
  DestructorGuard dg(this);

  assert(inFlight_ > 0);
  --inFlight_;
  if (options_.maxInFlight > 0 && !isSubRequest) {
    assert(realRequestsInFlight_ > 0);
    if (--realRequestsInFlight_ < options_.maxInFlight) {
      resume(PAUSE_THROTTLED);
    }
  }

  checkClosed();
}

void McServerSession::reply(std::unique_ptr<WriteBuffer> wb, uint64_t reqid) {
  DestructorGuard dg(this);

  if (parser_.outOfOrder()) {
    queueWrite(std::move(wb));
  } else {
    if (reqid == headReqid_) {
      /* head of line reply, write it and all contiguous blocked replies */
      queueWrite(std::move(wb));
      auto it = blockedReplies_.find(++headReqid_);
      while (it != blockedReplies_.end()) {
        queueWrite(std::move(it->second));
        blockedReplies_.erase(it);
        it = blockedReplies_.find(++headReqid_);
      }
    } else {
      /* can't write this reply now, save for later */
      blockedReplies_.emplace(reqid, std::move(wb));
    }
  }
}

void McServerSession::processMultiOpEnd() {
  currentMultiop_->recordEnd(tailReqid_++);
  currentMultiop_.reset();
}

void McServerSession::beginClose(folly::StringPiece reason) {
  if (options_.goAwayTimeout.count() == 0 ||
      parser_.protocol() != mc_caret_protocol) {
    close();
  } else {
    McServerRequestContext ctx(*this, kCaretConnectionControlReqId);
    GoAwayRequest goAway;
    goAway.reason() = reason.str();
    McServerRequestContext::reply(std::move(ctx), std::move(goAway));
    goAwayTimeout_ = folly::AsyncTimeout::schedule(
        options_.goAwayTimeout, eventBase_, [this]() noexcept { close(); });
  }
}

void McServerSession::close() {
  DestructorGuard dg(this);

  // Reset timeout if set, since we're shutting down anyway.
  goAwayTimeout_ = nullptr;

  // Regardless of the reason we're closing, we should immediately stop reading
  // from the socket or we may get into invalid state.
  if (transport_) {
    transport_->setReadCB(nullptr);
  }

  if (currentMultiop_) {
    /* If we got closed in the middle of a multiop request,
       process it as if we saw the multi-op end sentinel */
    processMultiOpEnd();
  }

  if (state_ == STREAMING) {
    state_ = CLOSING;
    stateCb_.onCloseStart(*this);
  }

  checkClosed();
}

void McServerSession::getReadBuffer(void** bufReturn, size_t* lenReturn) {
  curBuffer_ = parser_.getReadBuffer();
  *bufReturn = curBuffer_.first;
  *lenReturn = curBuffer_.second;
}

void McServerSession::readDataAvailable(size_t len) noexcept {
  DestructorGuard dg(this);
  if (!parser_.readDataAvailable(len)) {
    close();
  }
}

void McServerSession::readEOF() noexcept {
  close();
}

void McServerSession::readErr(const folly::AsyncSocketException&) noexcept {
  close();
}

void McServerSession::multiOpEnd() {
  DestructorGuard dg(this);

  if (state_ != STREAMING) {
    return;
  }

  processMultiOpEnd();
}

void McServerSession::onRequest(
    McVersionRequest&& req,
    bool /* noreply = false */) {
  uint64_t reqid = 0;
  if (!parser_.outOfOrder()) {
    reqid = tailReqid_++;
  }

  McServerRequestContext ctx(*this, reqid);

  if (options_.defaultVersionHandler) {
    McVersionReply reply(mc_res_ok);
    reply.value() =
        folly::IOBuf(folly::IOBuf::COPY_BUFFER, options_.versionString);
    McServerRequestContext::reply(std::move(ctx), std::move(reply));
    return;
  }

  onRequest_->requestReady(std::move(ctx), std::move(req));
}

void McServerSession::onRequest(McShutdownRequest&&, bool) {
  uint64_t reqid = 0;
  if (!parser_.outOfOrder()) {
    reqid = tailReqid_++;
  }
  McServerRequestContext ctx(*this, reqid, true /* noReply */);
  McServerRequestContext::reply(std::move(ctx), McShutdownReply(mc_res_ok));
  stateCb_.onShutdown();
}

void McServerSession::onRequest(McQuitRequest&&, bool) {
  uint64_t reqid = 0;
  if (!parser_.outOfOrder()) {
    reqid = tailReqid_++;
  }
  McServerRequestContext ctx(*this, reqid, true /* noReply */);
  McServerRequestContext::reply(std::move(ctx), McQuitReply(mc_res_ok));
  close();
}

void McServerSession::caretRequestReady(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& reqBody) {
  DestructorGuard dg(this);

  assert(parser_.protocol() == mc_caret_protocol);
  assert(parser_.outOfOrder());

  if (state_ != STREAMING) {
    return;
  }

  updateCompressionCodecIdRange(headerInfo);

  if (headerInfo.reqId == kCaretConnectionControlReqId) {
    processConnectionControlMessage(headerInfo);
    return;
  }

  McServerRequestContext ctx(*this, headerInfo.reqId);

  if (McVersionRequest::typeId == headerInfo.typeId &&
      options_.defaultVersionHandler) {
    McVersionReply versionReply(mc_res_ok);
    versionReply.value() =
        folly::IOBuf(folly::IOBuf::COPY_BUFFER, options_.versionString);
    McServerRequestContext::reply(std::move(ctx), std::move(versionReply));
  } else {
    try {
      onRequest_->caretRequestReady(headerInfo, reqBody, std::move(ctx));
    } catch (const std::exception& e) {
      // Ideally, ctx would be created after successful parsing of Caret data.
      // For now, if ctx hasn't been moved out of, mark as replied.
      ctx.replied_ = true;
      throw;
    }
  }
}

void McServerSession::processConnectionControlMessage(
    const UmbrellaMessageInfo& headerInfo) {
  DestructorGuard dg(this);
  switch (headerInfo.typeId) {
    case GoAwayAcknowledgement::typeId: {
      // Client acknowledged GoAway, no new requests should be received, start
      // closing the connection.
      close();
      break;
    }
    default:
      // Unknown connection controll message, ignore it.
      break;
  }
}

void McServerSession::updateCompressionCodecIdRange(
    const UmbrellaMessageInfo& headerInfo) noexcept {
  if (headerInfo.supportedCodecsSize == 0 || !compressionCodecMap_) {
    codecIdRange_ = CodecIdRange::Empty;
  } else {
    codecIdRange_ = {headerInfo.supportedCodecsFirstId,
                     headerInfo.supportedCodecsSize};
  }
}

void McServerSession::parseError(mc_res_t result, folly::StringPiece reason) {
  DestructorGuard dg(this);

  if (state_ != STREAMING) {
    return;
  }

  McVersionReply errorReply(result);
  errorReply.message() = reason.str();
  errorReply.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, reason.str());
  McServerRequestContext::reply(
      McServerRequestContext(*this, tailReqid_++), std::move(errorReply));
  close();
}

void McServerSession::ensureWriteBufs() {
  if (writeBufs_ == nullptr) {
    writeBufs_ = std::make_unique<WriteBufferQueue>(parser_.protocol());
  }
}

void McServerSession::queueWrite(std::unique_ptr<WriteBuffer> wb) {
  if (wb == nullptr) {
    return;
  }
  if (options_.singleWrite) {
    if (UNLIKELY(debugFifo_.isConnected())) {
      writeToDebugFifo(wb.get());
    }
    const struct iovec* iovs = wb->getIovsBegin();
    size_t iovCount = wb->getIovsCount();
    writeBufs_->push(std::move(wb));
    transport_->writev(this, iovs, iovCount);
    if (!writeBufs_->empty()) {
      /* We only need to pause if the sendmsg() call didn't write everything
         in one go */
      pause(PAUSE_WRITE);
    }
  } else {
    pendingWrites_->pushBack(std::move(wb));

    if (!writeScheduled_) {
      eventBase_.runInLoop(&sendWritesCallback_, /* thisIteration= */ true);
      writeScheduled_ = true;
    }
  }
}

void McServerSession::sendWrites() {
  DestructorGuard dg(this);

  writeScheduled_ = false;

  folly::small_vector<struct iovec, kIovecVectorSize> iovs;
  while (!pendingWrites_->empty()) {
    auto wb = pendingWrites_->popFront();
    if (!wb->noReply()) {
      if (UNLIKELY(debugFifo_.isConnected())) {
        writeToDebugFifo(wb.get());
      }
      iovs.insert(
          iovs.end(),
          wb->getIovsBegin(),
          wb->getIovsBegin() + wb->getIovsCount());
    }
    if (pendingWrites_->empty()) {
      wb->markEndOfBatch();
    }
    writeBufs_->push(std::move(wb));
  }

  transport_->writev(this, iovs.data(), iovs.size());
}

void McServerSession::writeToDebugFifo(const WriteBuffer* wb) noexcept {
  if (!wb->isSubRequest()) {
    debugFifo_.startMessage(MessageDirection::Sent, wb->typeId());
    hasPendingMultiOp_ = false;
  } else {
    // Handle multi-op
    if (!hasPendingMultiOp_) {
      debugFifo_.startMessage(MessageDirection::Sent, wb->typeId());
      hasPendingMultiOp_ = true;
    }
    if (wb->isEndContext()) {
      // Multi-op replies always finish with an end context
      hasPendingMultiOp_ = false;
    }
  }
  debugFifo_.writeData(wb->getIovsBegin(), wb->getIovsCount());
}

void McServerSession::completeWrite() {
  writeBufs_->pop(!options_.singleWrite /* popBatch */);
}

void McServerSession::writeSuccess() noexcept {
  DestructorGuard dg(this);
  completeWrite();

  assert(writeBufs_ != nullptr);
  if (writeBufs_->empty() && state_ == STREAMING) {
    stateCb_.onWriteQuiescence(*this);
    /* No-op if not paused */
    resume(PAUSE_WRITE);
  }
}

void McServerSession::writeErr(
    size_t /* bytesWritten */,
    const folly::AsyncSocketException&) noexcept {
  DestructorGuard dg(this);
  completeWrite();
  close();
}

bool McServerSession::handshakeVer(
    folly::AsyncSSLSocket* sock,
    bool preverifyOk,
    X509_STORE_CTX* ctx) noexcept {
  return McServerSSLUtil::verifySSL(sock, preverifyOk, ctx);
}

void McServerSession::handshakeSuc(folly::AsyncSSLSocket* sock) noexcept {
  auto cert = sock->getPeerCert();
  if (cert == nullptr) {
    return;
  }
  auto sub = X509_get_subject_name(cert.get());
  if (sub != nullptr) {
    char cn[ub_common_name + 1];
    const auto res =
        X509_NAME_get_text_by_NID(sub, NID_commonName, cn, ub_common_name);
    if (res > 0) {
      clientCommonName_.assign(std::string(cn, res));
    }
  }
}

void McServerSession::handshakeErr(
    folly::AsyncSSLSocket*,
    const folly::AsyncSocketException&) noexcept {}
} // memcache
} // facebook
