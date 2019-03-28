/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "McServerSession.h"

#include <memory>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/small_vector.h>

#include "mcrouter/lib/debug/FifoManager.h"
#include "mcrouter/lib/network/McFizzServer.h"
#include "mcrouter/lib/network/McSSLUtil.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
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
    McServerSession::Queue* queue,
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

  if (queue) {
    queue->push_front(*ptr);
  }

  // For secure connections, we need to delay calling the onAccepted client
  // callback until the handshake is complete.
  if (ptr->securityMech() == SecurityMech::NONE) {
    ptr->onAccepted();
  }

  return *ptr;
}

void McServerSession::applySocketOptions(
    folly::AsyncSocket& socket,
    const AsyncMcServerWorkerOptions& opts) {
  socket.setMaxReadsPerEvent(opts.maxReadsPerEvent);
  socket.setNoDelay(true);
  if (opts.tcpZeroCopyThresholdBytes > 0) {
    socket.setZeroCopy(true);
  }
  socket.setSendTimeout(opts.sendTimeout.count());
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
      sendWritesCallback_(*this),
      compressionCodecMap_(codecMap),
      parser_(*this, options_.minBufferSize, options_.maxBufferSize),
      userCtxt_(userCtxt),
      zeroCopySessionCB_(*this) {
  try {
    transport_->getPeerAddress(&socketAddress_);
  } catch (const std::exception& e) {
    // std::system_error or other exception, leave IP address empty
    LOG(WARNING) << "Failed to get socket address: " << e.what();
  }

  if (auto socket = transport_->getUnderlyingTransport<McFizzServer>()) {
    socket->accept(this);
  }
}

SecurityMech McServerSession::securityMech() const noexcept {
  if (!transport_) {
    return SecurityMech::NONE;
  }
  if (transport_->getUnderlyingTransport<McFizzServer>()) {
    return SecurityMech::TLS13_FIZZ;
  }
  if (transport_->getUnderlyingTransport<folly::AsyncSSLSocket>()) {
    return SecurityMech::TLS;
  }
  if (transport_->getSecurityProtocol() == McSSLUtil::kTlsToPlainProtocolName) {
    return SecurityMech::TLS_TO_PLAINTEXT;
  }
  return SecurityMech::NONE;
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
  if (!inFlight_ &&
      (!isZeroCopyEnabled() || (writeBufs_.zeroCopyQueueSize() == 0))) {
    assert(pendingWrites_.empty());

    if (state_ == CLOSING) {
      /* It's possible to call close() more than once from the same stack.
         Prevent second close() from doing anything */
      state_ = CLOSED;
      if (transport_) {
        /* prevent readEOF() from being called */
        transport_->setReadCB(nullptr);
        transport_.reset();
      }
      onCloseFinish();
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
    onCloseStart();
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
    McVersionReply reply(carbon::Result::OK);
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
  McServerRequestContext::reply(
      std::move(ctx), McShutdownReply(carbon::Result::OK));
  stateCb_.onShutdown();
}

void McServerSession::onRequest(McQuitRequest&&, bool) {
  uint64_t reqid = 0;
  if (!parser_.outOfOrder()) {
    reqid = tailReqid_++;
  }
  McServerRequestContext ctx(*this, reqid, true /* noReply */);
  McServerRequestContext::reply(
      std::move(ctx), McQuitReply(carbon::Result::OK));
  close();
}

void McServerSession::caretRequestReady(
    const CaretMessageInfo& headerInfo,
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
    McVersionReply versionReply(carbon::Result::OK);
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
    const CaretMessageInfo& headerInfo) {
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
    const CaretMessageInfo& headerInfo) noexcept {
  if (headerInfo.supportedCodecsSize == 0 || !compressionCodecMap_) {
    codecIdRange_ = CodecIdRange::Empty;
  } else {
    codecIdRange_ = {headerInfo.supportedCodecsFirstId,
                     headerInfo.supportedCodecsSize};
  }
}

void McServerSession::parseError(
    carbon::Result result,
    folly::StringPiece reason) {
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

void McServerSession::sendZeroCopyIOBuf(
    WriteBuffer& wbuf,
    const struct iovec* iovs,
    size_t iovsCount) {
  DestructorGuard dg(this);
  using BufferContext = std::tuple<
      std::reference_wrapper<WriteBuffer>,
      std::reference_wrapper<WriteBufferQueue>,
      bool /* batch */,
      DestructorGuard>;

  // IOBuf FreeFn is a function pointer, so cannot use lambdas. Pass in
  // destructor guard to ensure that McServerSession is not destructed before
  // this IOBuf is destroyed.
  auto wbufInfo = std::make_unique<BufferContext>(
      std::ref(wbuf), std::ref(writeBufs_), !options_.singleWrite, dg);

  std::unique_ptr<folly::IOBuf> chainTail;

  for (size_t i = 0; i < iovsCount; i++) {
    size_t len = iovs[i].iov_len;
    if (len > 0) {
      std::unique_ptr<folly::IOBuf> iobuf;
      if (!chainTail) {
        iobuf = folly::IOBuf::takeOwnership(
            iovs[i].iov_base,
            len,
            [](void* /* unused */, void* userData) {
              auto bufferContext = std::unique_ptr<BufferContext>(
                  reinterpret_cast<BufferContext*>(userData));
              auto& q = std::get<1>(*bufferContext).get();
              auto& wb = std::get<0>(*bufferContext).get();
              auto batch = std::get<2>(*bufferContext);
              q.releaseZeroCopyChain(wb, batch);
            },
            wbufInfo.release(),
            true /* freeOnError */);
        chainTail = std::move(iobuf);
      } else {
        // The IOBufs not at the head of chain have a noop free function given
        // that the head of chain will free the entire chain.
        iobuf = folly::IOBuf::takeOwnership(
            iovs[i].iov_base,
            len,
            [](void* /* unused */, void* /* unused */) {},
            nullptr,
            true /* freeOnError */);
        chainTail->prependChain(std::move(iobuf));
      }
    }
  }

  zeroCopySessionCB_.incCallbackPending();
  transport_->writeChain(
      &zeroCopySessionCB_ /* write cb */,
      std::move(chainTail),
      folly::WriteFlags::WRITE_MSG_ZEROCOPY);
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
    if (isZeroCopyEnabled() && wb->shouldApplyZeroCopy()) {
      auto& wbuf = writeBufs_.insertZeroCopy(std::move(wb));
      // Creates a chain of IOBufs and uses TCP copy avoidance
      sendZeroCopyIOBuf(wbuf, iovs, iovCount);
      if (zeroCopySessionCB_.getCallbackPending() > 0) {
        pause(PAUSE_WRITE);
      }
    } else {
      writeBufs_.push(std::move(wb));
      transport_->writev(this, iovs, iovCount);
      if (!writeBufs_.empty()) {
        /* We only need to pause if the sendmsg() call didn't write everything
           in one go */
        pause(PAUSE_WRITE);
      }
    }
  } else {
    if (!writeScheduled_) {
      eventBase_.runInLoop(&sendWritesCallback_, /* thisIteration= */ true);
      writeScheduled_ = true;
    }
    if (isZeroCopyEnabled() && wb->shouldApplyZeroCopy()) {
      isNextWriteBatchZeroCopy_ = true;
    }
    pendingWrites_.pushBack(std::move(wb));
  }
}

void McServerSession::sendWrites() {
  DestructorGuard dg(this);

  if (pendingWrites_.empty()) {
    return;
  }

  bool doZeroCopy = isNextWriteBatchZeroCopy_;
  writeScheduled_ = false;
  isNextWriteBatchZeroCopy_ = false;

  folly::small_vector<struct iovec, kIovecVectorSize> iovs;
  WriteBuffer* firstBuf = nullptr;
  while (!pendingWrites_.empty()) {
    auto wb = pendingWrites_.popFront();
    if (!wb->noReply()) {
      if (UNLIKELY(debugFifo_.isConnected())) {
        writeToDebugFifo(wb.get());
      }
      iovs.insert(
          iovs.end(),
          wb->getIovsBegin(),
          wb->getIovsBegin() + wb->getIovsCount());
    }
    if (pendingWrites_.empty()) {
      wb->markEndOfBatch();
    }
    if (doZeroCopy) {
      if (!firstBuf) {
        firstBuf = &writeBufs_.insertZeroCopy(std::move(wb));
      } else {
        writeBufs_.insertZeroCopy(std::move(wb));
      }
    } else {
      writeBufs_.push(std::move(wb));
    }
  }

  if (doZeroCopy) {
    assert(firstBuf != nullptr);
    sendZeroCopyIOBuf(*firstBuf, iovs.data(), iovs.size());
  } else {
    transport_->writev(this, iovs.data(), iovs.size());
  }
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
  writeBufs_.pop(!options_.singleWrite /* popBatch */);
}

void McServerSession::writeSuccess() noexcept {
  DestructorGuard dg(this);
  completeWrite();

  if (writeBufs_.empty() && state_ == STREAMING) {
    onWriteQuiescence();
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
  return McSSLUtil::verifySSL(sock, preverifyOk, ctx);
}

void McServerSession::handshakeSuc(folly::AsyncSSLSocket* sock) noexcept {
  DestructorGuard dg(this);

  auto cert = sock->getPeerCert();
  if (cert != nullptr) {
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
  McSSLUtil::finalizeServerSSL(transport_.get());

  if (McSSLUtil::negotiatedPlaintextFallback(*sock)) {
    auto fallback = McSSLUtil::moveToPlaintext(*sock);
    CHECK(fallback);
    auto asyncSock = fallback->getUnderlyingTransport<folly::AsyncSocket>();
    CHECK(asyncSock);
    applySocketOptions(*asyncSock, options_);
    transport_.reset(fallback.release());
  }

  // sock is currently wrapped by transport_, but underlying socket may
  // change by end of this function (mainly due to negotiatedPlaintextFallback).
  transport_->setReadCB(this);

  onAccepted();
}

void McServerSession::handshakeErr(
    folly::AsyncSSLSocket*,
    const folly::AsyncSocketException& e) noexcept {
  LOG(ERROR) << "SSL Handshake failure: " << e.what();
  close();
}

void McServerSession::fizzHandshakeSuccess(
    fizz::server::AsyncFizzServer* transport) noexcept {
  DestructorGuard dg(this);

  auto cert = transport->getPeerCert();
  if (cert == nullptr) {
    onAccepted();
    return;
  }
  auto sub = X509_get_subject_name(cert.get());
  if (sub != nullptr) {
    std::array<char, ub_common_name + 1> cn{};
    const auto res = X509_NAME_get_text_by_NID(
        sub, NID_commonName, cn.data(), ub_common_name);
    if (res > 0) {
      clientCommonName_.assign(std::string(cn.data(), res));
    }
  }
  McSSLUtil::finalizeServerSSL(transport);
  onAccepted();
}

void McServerSession::fizzHandshakeError(
    fizz::server::AsyncFizzServer*,
    folly::exception_wrapper e) noexcept {
  LOG(ERROR) << "Fizz Handshake failure: " << e.what();
  close();
}

void McServerSession::fizzHandshakeAttemptFallback(
    std::unique_ptr<folly::IOBuf> clientHello) {
  DestructorGuard dg(this);
  auto transport = transport_->getUnderlyingTransport<McFizzServer>();
  CHECK(transport) << " transport should not be nullptr";
  transport->setReadCB(nullptr);
  auto evb = transport->getEventBase();
  auto socket = transport->getUnderlyingTransport<folly::AsyncSocket>();
  CHECK(socket) << " socket should not be nullptr";
  auto fd = socket->detachNetworkSocket().toFd();
  const auto& ctx = transport->getFallbackContext();

  folly::AsyncSSLSocket::UniquePtr sslSocket(new folly::AsyncSSLSocket(
      ctx, evb, folly::NetworkSocket::fromFd(fd), true /* server */));
  sslSocket->setPreReceivedData(std::move(clientHello));
  sslSocket->enableClientHelloParsing();
  sslSocket->forceCacheAddrOnFailure(true);
  // need to re apply the socket options
  applySocketOptions(*sslSocket, options_);

  // We need to reset the transport before calling sslAccept(). The reason is
  // that sslAccept() may call some callbacks inline (e.g. handshakeSuc()) that
  // may need to see the actual transport_.
  transport_.reset(sslSocket.release());
  auto underlyingSslSocket =
      transport_->getUnderlyingTransport<folly::AsyncSSLSocket>();
  DCHECK(underlyingSslSocket) << "Underlying socket should be AsyncSSLSocket";
  underlyingSslSocket->sslAccept(this);
}

void McServerSession::onAccepted() {
  DCHECK(!onAcceptedCalled_);
  DCHECK(transport_);
  debugFifo_ = getDebugFifo(
      options_.debugFifoPath, transport_.get(), onRequest_->name());
  parser_.setDebugFifo(&debugFifo_);
  onAcceptedCalled_ = true;
  stateCb_.onAccepted(*this);
}

void McServerSession::onCloseStart() {
  stateCb_.onCloseStart(*this);
}

void McServerSession::onCloseFinish() {
  stateCb_.onCloseFinish(*this, onAcceptedCalled_);
}

void McServerSession::onWriteQuiescence() {
  stateCb_.onWriteQuiescence(*this);
}

} // namespace memcache
} // namespace facebook
