/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "AsyncMcClientImpl.h"

#include "folly/Memory.h"
#include "folly/io/async/EventBase.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "thrift/lib/cpp/async/TAsyncSSLSocket.h"

namespace facebook { namespace memcache {

constexpr size_t kReadBufferSize = 4096;

namespace detail {
class OnEventBaseDestructionCallback : public folly::EventBase::LoopCallback {
public:
  explicit OnEventBaseDestructionCallback(AsyncMcClientImpl& client)
      : client_(client) {}
  ~OnEventBaseDestructionCallback() {}
  virtual void runLoopCallback() noexcept override {
    client_.closeNow();
  }
 private:
  AsyncMcClientImpl& client_;
};
} // detail

/**
 * A callback class for network writing.
 *
 * We use it instead of simple std::function, because it will safely cancel
 * callback event when destructed.
 */
class AsyncMcClientImpl::WriterLoop : public folly::EventBase::LoopCallback {
 public:
  explicit WriterLoop(AsyncMcClientImpl& client)
      : client_(client) {}
  ~WriterLoop() {}
  virtual void runLoopCallback() noexcept override {
    client_.pushMessages();
  }
 private:
  AsyncMcClientImpl& client_;
};

/**
 * A callback class for handling timeouts.
 */
class AsyncMcClientImpl::TimeoutCallback : public folly::AsyncTimeout {
 public:
  TimeoutCallback(AsyncMcClientImpl& client) : client_(client) {
    attachEventBase(&client_.eventBase_,
                    folly::TimeoutManager::InternalEnum::NORMAL);
  }

  void timeoutExpired() noexcept {
    client_.timeoutExpired();
  }
 private:
  AsyncMcClientImpl& client_;
};

AsyncMcClientImpl::AsyncMcClientImpl(
    folly::EventBase& eventBase,
    ConnectionOptions options)
    : eventBase_(eventBase),
      serializer_(options.protocol,
                  std::bind(&AsyncMcClientImpl::onParseReply,
                            this,
                            std::placeholders::_1,
                            std::placeholders::_2),
                  std::bind(&AsyncMcClientImpl::onParseError,
                            this,
                            std::placeholders::_1)),
      connectionOptions_(std::move(options)),
      outOfOrder_(connectionOptions_.protocol == mc_umbrella_protocol),
      timeoutCallback_(folly::make_unique<TimeoutCallback>(*this)),
      writer_(folly::make_unique<WriterLoop>(*this)),
      eventBaseDestructionCallback_(
        folly::make_unique<detail::OnEventBaseDestructionCallback>(*this)) {
  eventBase_.runOnDestruction(eventBaseDestructionCallback_.get());
}

std::shared_ptr<AsyncMcClientImpl> AsyncMcClientImpl::create(
    folly::EventBase& eventBase,
    ConnectionOptions options) {
  auto client = std::shared_ptr<AsyncMcClientImpl>(
    new AsyncMcClientImpl(eventBase, std::move(options)), Destructor());
  client->selfPtr_ = client;
  return client;
}

void AsyncMcClientImpl::closeNow() {
  DestructorGuard dg(this);

  if (connectionState_ != ConnectionState::DOWN) {
    processShutdown(true /* isAborting */);
    if (socket_) {
      socket_->closeNow();
    }
  }

  timeoutCallback_->cancelTimeout();
}

void AsyncMcClientImpl::setStatusCallbacks(
    std::function<void()> onUp,
    std::function<void(const TransportException&)> onDown) {
  DestructorGuard dg(this);

  statusCallbacks_ = ConnectionStatusCallbacks {
    std::move(onUp),
    std::move(onDown)
  };

  if (connectionState_ == ConnectionState::UP && statusCallbacks_.onUp) {
    statusCallbacks_.onUp();
  }
}

AsyncMcClientImpl::~AsyncMcClientImpl() {
  assert(sendQueue_.empty());
  assert(writeQueue_.empty());
  assert(pendingReplyQueue_.empty());
  if (socket_) {
    // Close the socket immediately. We need to process all callbacks, such as
    // readEOF and connectError, before we exit destructor.
    socket_->closeNow();
  }
  eventBaseDestructionCallback_.reset();
}

size_t AsyncMcClientImpl::getPendingRequestCount() const {
  return sendQueue_.size();
}

size_t AsyncMcClientImpl::getInflightRequestCount() const {
  return writeQueue_.size() + pendingReplyQueue_.size();
}

void AsyncMcClientImpl::setThrottle(size_t maxInflight, size_t maxPending) {
  maxInflight_ = maxInflight;
  maxPending_ = maxPending;
}

void AsyncMcClientImpl::scheduleNextWriterLoop() {
  if (connectionState_ == ConnectionState::UP && !writeScheduled_ &&
      !sendQueue_.empty()) {
    writeScheduled_ = true;
    eventBase_.runInLoop(writer_.get());
  }
}

void AsyncMcClientImpl::cancelWriterCallback() {
  writeScheduled_ = false;
  writer_->cancelLoopCallback();
}

void AsyncMcClientImpl::pushMessages() {
  DestructorGuard dg(this);

  assert(connectionState_ == ConnectionState::UP);
  size_t numToSend = sendQueue_.size();
  if (maxInflight_ != 0) {
    if (maxInflight_ <= getInflightRequestCount()) {
      numToSend = 0;
    } else {
      numToSend = std::min(numToSend,
                           maxInflight_ - getInflightRequestCount());
    }
  }
  while (!sendQueue_.empty() && numToSend > 0 &&
         /* we might be already not UP, because of failed writev */
         connectionState_ == ConnectionState::UP) {
    auto& req = writeQueue_.pushBack(sendQueue_.popFront());
    req.sentAt = std::chrono::steady_clock::now();

    socket_->writev(this, req.reqContext.getIovs(),
      req.reqContext.getIovsCount(),
      numToSend == 1 ? apache::thrift::async::WriteFlags::NONE
                     : apache::thrift::async::WriteFlags::CORK);
    --numToSend;
  }
  writeScheduled_ = false;
  scheduleNextWriterLoop();
}

void AsyncMcClientImpl::timeoutExpired() {
  DestructorGuard dg(this);

  timeoutScheduled_ = false;
  auto currentTime = std::chrono::steady_clock::now();
  // process all requests that were replied/timed out.
  while (!pendingReplyQueue_.empty()) {
    if (currentTime - pendingReplyQueue_.front().sentAt >=
        connectionOptions_.timeout) {
      reply(pendingReplyQueue_.popFront(), McReply(mc_res_timeout));
    } else {
      scheduleNextTimeout();
      break;
    }
  }
}

namespace {
// Chrono round up duration_cast
template <class T, class Rep, class Period>
T round_up(std::chrono::duration<Rep, Period> d) {
  T result = std::chrono::duration_cast<T>(d);
  if (result < d) {
    ++result;
  }
  return result;
}

apache::thrift::async::TAsyncSocket::OptionMap createTCPKeepAliveOptions(
    int cnt, int idle, int interval) {
  apache::thrift::async::TAsyncSocket::OptionMap options;
  // 0 means KeepAlive is disabled.
  if (cnt != 0) {
#ifdef SO_KEEPALIVE
    apache::thrift::async::TAsyncSocket::OptionMap::key_type key;
    key.level = SOL_SOCKET;
    key.optname = SO_KEEPALIVE;
    options[key] = 1;

    key.level = IPPROTO_TCP;

#ifdef TCP_KEEPCNT
    key.optname = TCP_KEEPCNT;
    options[key] = cnt;
#endif // TCP_KEEPCNT

#ifdef TCP_KEEPIDLE
    key.optname = TCP_KEEPIDLE;
    options[key] = idle;
#endif // TCP_KEEPIDLE

#ifdef TCP_KEEPINTVL
    key.optname = TCP_KEEPINTVL;
    options[key] = interval;
#endif // TCP_KEEPINTVL

#endif // SO_KEEPALIVE
  }

  return std::move(options);
}

} // namespace

void AsyncMcClientImpl::scheduleNextTimeout() {
  if (!timeoutScheduled_ && connectionOptions_.timeout.count() > 0 &&
      !pendingReplyQueue_.empty()) {
    timeoutScheduled_ = true;
    const auto& req = pendingReplyQueue_.front();
    // Rount up, or otherwise we might get stuck processing timeouts of 0ms.
    timeoutCallback_->scheduleTimeout(
      round_up<std::chrono::milliseconds>(req.sentAt +
        connectionOptions_.timeout - std::chrono::steady_clock::now()));
  }
}

void AsyncMcClientImpl::reply(std::unique_ptr<ReqInfo> req, McReply&& reply) {
  idMap_.erase(req->id);
  req->replyCallback(std::move(reply));
}

void AsyncMcClientImpl::replyReceived(uint64_t id, McMsgRef&& msg) {
  // We could have already timed out all request.
  if (pendingReplyQueue_.empty()) {
    return;
  }

  if (outOfOrder_) {
    ReqInfo* value = idMap_[id];
    // Check that it wasn't previously replied with timeout (e.g. we hadn't
    // already removed it).
    if (value != nullptr) {
      auto req = pendingReplyQueue_.extract(
        pendingReplyQueue_.iterator_to(*value));
      setOpAndReply(std::move(req), std::move(msg));
    }
  } else {
    // Check that it wasn't previously replied with timeout (e.g. we hadn't
    // already removed it).
    if (pendingReplyQueue_.front().id == id) {
      auto req = pendingReplyQueue_.popFront();
      setOpAndReply(std::move(req), std::move(msg));
    }
  }
}

void AsyncMcClientImpl::setOpAndReply(std::unique_ptr<ReqInfo> req,
                                      McMsgRef&& msg) {
  const_cast<mc_msg_t*>(msg.get())->op = req->op;
  mc_res_t result = msg->result;
  reply(std::move(req), McReply(result, std::move(msg)));
}

void AsyncMcClientImpl::attemptConnection() {
  assert(connectionState_ == ConnectionState::DOWN);

  connectionState_ = ConnectionState::CONNECTING;

  if (connectionOptions_.sslContextProvider) {
    socket_.reset(new apache::thrift::async::TAsyncSSLSocket(
      connectionOptions_.sslContextProvider(), &eventBase_));
  } else {
    socket_.reset(new apache::thrift::async::TAsyncSocket(&eventBase_));
  }

  auto address = apache::thrift::transport::TSocketAddress(
    connectionOptions_.host, connectionOptions_.port,
    /* allowNameLookup */ true);

  auto socketOptions = createTCPKeepAliveOptions(
    connectionOptions_.tcpKeepAliveCount, connectionOptions_.tcpKeepAliveIdle,
    connectionOptions_.tcpKeepAliveInterval);

  socket_->connect(this, address, connectionOptions_.timeout.count(),
                   socketOptions);
  socket_->setSendTimeout(connectionOptions_.timeout.count());
}

void AsyncMcClientImpl::connectSuccess() noexcept {
  assert(connectionState_ == ConnectionState::CONNECTING);
  DestructorGuard dg(this);
  connectionState_ = ConnectionState::UP;

  if (statusCallbacks_.onUp) {
    statusCallbacks_.onUp();
  }

  // We would never attempt to connect without having any messages to send.
  assert(!sendQueue_.empty());
  // We might have successfuly reconnected after error, so we need to restart
  // our msg id counter.
  nextInflightMsgId_ = sendQueue_.front().id;

  scheduleNextWriterLoop();
  serializer_ = McProtocolSerializer(
    connectionOptions_.protocol,
    std::bind(&AsyncMcClientImpl::onParseReply, this, std::placeholders::_1,
              std::placeholders::_2),
    std::bind(&AsyncMcClientImpl::onParseError, this, std::placeholders::_1));
  socket_->setReadCallback(this);
}

void AsyncMcClientImpl::connectError(const TransportException& ex) noexcept {
  assert(connectionState_ == ConnectionState::CONNECTING);
  DestructorGuard dg(this);

  mc_res_t error;

  if (ex.getType() == TransportException::TIMED_OUT) {
    error = mc_res_connect_timeout;
  } else {
    error = mc_res_connect_error;
  }

  while (!sendQueue_.empty()) {
    reply(sendQueue_.popFront(), McReply(error));
  }

  assert(writeQueue_.empty());
  assert(pendingReplyQueue_.empty());

  connectionState_ = ConnectionState::DOWN;
  // We don't need it anymore, so let it perform complete cleanup.
  socket_.reset();

  if (statusCallbacks_.onDown) {
    statusCallbacks_.onDown(ex);
  }
}

void AsyncMcClientImpl::processShutdown(bool isAborting) {
  DestructorGuard dg(this);
  switch (connectionState_) {
    case ConnectionState::UP: // on error, UP always transitions to ERROR state
      isAborting_ = isAborting;
      if (writeScheduled_) {
        // Cancel loop callback, or otherwise we might attempt to write
        // something while processing error state.
        cancelWriterCallback();
      }
      connectionState_ = ConnectionState::ERROR;
      // We're already in ERROR state, no need to listen for reads.
      socket_->setReadCallback(nullptr);
      // We can safely close connection, it will stop all writes.
      socket_->close();

      /* fallthrough */

    case ConnectionState::ERROR:
      while (!pendingReplyQueue_.empty()) {
        reply(pendingReplyQueue_.popFront(),
              McReply(isAborting ? mc_res_aborted : mc_res_remote_error));
      }
      if (writeQueue_.empty()) {
        // No need to send any of remaining requests if we're aborting.
        if (isAborting) {
          while (!sendQueue_.empty()) {
            reply(sendQueue_.popFront(), McReply(mc_res_aborted));
          }
        }

        // This is a last processShutdown() for this error and it is safe
        // to go DOWN.
        if (statusCallbacks_.onDown) {
          statusCallbacks_.onDown(TransportException::INTERNAL_ERROR);
        }

        connectionState_ = ConnectionState::DOWN;
        // We don't need it anymore, so let it perform complete cleanup.
        socket_.reset();

        // In case we still have some pending requests, then try reconnecting
        // immediately.
        if (!sendQueue_.empty()) {
          attemptConnection();
        }
      }
      return;
    case ConnectionState::CONNECTING:
      // connectError is not a remote error, it's processed in connectError.
    case ConnectionState::DOWN:
      // We shouldn't have any remote errors while not connected.
      CHECK(false);
  }
}

void AsyncMcClientImpl::getReadBuffer(void** bufReturn, size_t* lenReturn) {
  auto prealloc = buffer_.preallocate(kReadBufferSize, kReadBufferSize);
  *bufReturn = prealloc.first;
  *lenReturn = prealloc.second;
}

void AsyncMcClientImpl::readDataAvailable(size_t len) noexcept {
  DestructorGuard dg(this);
  buffer_.postallocate(len);
  serializer_.readData(buffer_.split(len));
}

void AsyncMcClientImpl::readEOF() noexcept {
  assert(connectionState_ == ConnectionState::UP);
  processShutdown();
}

void AsyncMcClientImpl::readError(const TransportException& ex) noexcept {
  assert(connectionState_ == ConnectionState::UP);
  LOG(ERROR) << "Failed to read from socket with remote endpoint \""
             << connectionOptions_.host << ":" << connectionOptions_.port
             << "\". Exception: " << ex.what();
  processShutdown();
}

void AsyncMcClientImpl::writeSuccess() noexcept {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);
  pendingReplyQueue_.pushBack(writeQueue_.popFront());
  scheduleNextTimeout();
}

void AsyncMcClientImpl::writeError(
    size_t bytesWritten, const TransportException& ex) noexcept {

  assert(connectionState_ == ConnectionState::UP ||
         connectionState_ == ConnectionState::ERROR);

  LOG(ERROR) << "Failed to write into socket with remote endpoint \""
             << connectionOptions_.host << ":" << connectionOptions_.port
             << "\", wrote " << bytesWritten
             << " bytes. Exception: " << ex.what();

  // We're already in an error state, so all requests in pendingReplyQueue_ will
  // be replied with an error.
  pendingReplyQueue_.pushBack(writeQueue_.popFront());
  processShutdown();
}

void AsyncMcClientImpl::onParseReply(uint64_t reqId, McMsgRef&& msg) {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);
  if (!outOfOrder_) {
    reqId = nextInflightMsgId_;
    incMsgId(nextInflightMsgId_);
  }
  replyReceived(reqId, std::move(msg));
}

void AsyncMcClientImpl::onParseError(parser_error_t error) {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);
  processShutdown();
}

void AsyncMcClientImpl::incMsgId(size_t& msgId) {
  ++msgId;
  if (UNLIKELY(msgId == 0)) {
    msgId = 1;
  }
}

}} // facebook::memcache
