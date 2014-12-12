/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "AsyncMcClientImpl.h"

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>
#include <thrift/lib/cpp/async/TAsyncSSLSocket.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/MockMcClientTransport.h"

namespace facebook { namespace memcache {

constexpr size_t kReadBufferSizeMin = 256;
constexpr size_t kReadBufferSizeMax = 4096;
constexpr uint16_t kBatchSizeStatWindow = 1024;

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
      connectionOptions_(std::move(options)),
      outOfOrder_(connectionOptions_.accessPoint.getProtocol() ==
                  mc_umbrella_protocol),
      timeoutCallback_(folly::make_unique<TimeoutCallback>(*this)),
      writer_(folly::make_unique<WriterLoop>(*this)),
      eventBaseDestructionCallback_(
        folly::make_unique<detail::OnEventBaseDestructionCallback>(*this)) {
  eventBase_.runOnDestruction(eventBaseDestructionCallback_.get());
}

std::shared_ptr<AsyncMcClientImpl> AsyncMcClientImpl::create(
    folly::EventBase& eventBase,
    ConnectionOptions options) {
  if (options.accessPoint.getProtocol() == mc_umbrella_protocol &&
      options.noNetwork) {
    throw std::logic_error("No network mode is not supported for umbrella "
                           "protocol yet!");
  }

  auto client = std::shared_ptr<AsyncMcClientImpl>(
    new AsyncMcClientImpl(eventBase, std::move(options)), Destructor());
  client->selfPtr_ = client;
  return client;
}

void AsyncMcClientImpl::closeNow() {
  DestructorGuard dg(this);

  if (socket_) {
    isAborting_ = true;
    // We need to destroy it immediately.
    socket_->closeNow();
    socket_.reset();
    isAborting_ = false;
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

std::pair<uint64_t, uint64_t> AsyncMcClientImpl::getBatchingStat() const {
  return { batchStatPrevious.first + batchStatCurrent.first,
           batchStatPrevious.second + batchStatCurrent.second };
}

void AsyncMcClientImpl::setThrottle(size_t maxInflight, size_t maxPending) {
  maxInflight_ = maxInflight;
  maxPending_ = maxPending;
}

void AsyncMcClientImpl::sendCommon(ReqInfo::UniquePtr req) {
  switch (req->reqContext.serializationResult()) {
    case McSerializedRequest::Result::OK:
      incMsgId(nextMsgId_);

      if (outOfOrder_) {
        idMap_[req->id] = req.get();
      }
      sendQueue_.pushBack(std::move(req));
      scheduleNextWriterLoop();
      if (connectionState_ == ConnectionState::DOWN) {
        // attempConnection may use a lot of stack memory, thus we need to run
        // it on a main context.
        fiber::runInMainContext([this] {
          attemptConnection();
        });
      }
      return;
    case McSerializedRequest::Result::BAD_KEY:
      reply(std::move(req), McReply(mc_res_bad_key));
      return;
    case McSerializedRequest::Result::ERROR:
      reply(std::move(req), McReply(mc_res_local_error));
      return;
  }
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
  // Record current batch size.
  batchStatCurrent.first += numToSend;
  ++batchStatCurrent.second;
  if (batchStatCurrent.second == kBatchSizeStatWindow) {
    batchStatPrevious = batchStatCurrent;
    batchStatCurrent = {0, 0};
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

void createTCPKeepAliveOptions(
    apache::thrift::async::TAsyncSocket::OptionMap& options,
    int cnt, int idle, int interval) {
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
}

const apache::thrift::async::TAsyncSocket::OptionKey getQoSOptionKey(
    sa_family_t addressFamily) {
  static const apache::thrift::async::TAsyncSocket::OptionKey kIpv4OptKey =
    {IPPROTO_IP, IP_TOS};
  static const apache::thrift::async::TAsyncSocket::OptionKey kIpv6OptKey =
    {IPPROTO_IPV6, IPV6_TCLASS};
  return (addressFamily == AF_INET) ? kIpv4OptKey : kIpv6OptKey;
}

uint64_t getQoSClass(int qosLevel) {
  static const uint64_t kDefaultClass = 0x00;
  static const uint64_t kLowestClass = 0x04;
  static const uint64_t kMediumClass = 0x40;
  static const uint64_t kHighestClass = 0x80;
  static const uint64_t kQoSClasses[] = {
    kDefaultClass, kLowestClass, kMediumClass, kHighestClass, kHighestClass
  };

  if (qosLevel < 0 || qosLevel > 4) {
    qosLevel = 0;
    failure::log("AsyncMcClient", failure::Category::kSystemError,
               "Invalid QoS value in AsyncMcClient");
  }

  return kQoSClasses[qosLevel];
}

void createQoSClassOption(
    apache::thrift::async::TAsyncSocket::OptionMap& options,
    const sa_family_t addressFamily, uint64_t qos) {
  const auto& optkey = getQoSOptionKey(addressFamily);
  options[optkey] = getQoSClass(qos);
}

void checkWhetherQoSIsApplied(const folly::SocketAddress& address,
                              int socketFd,
                              const ConnectionOptions& connectionOptions) {
  const auto& optkey = getQoSOptionKey(address.getFamily());

  static const int expectedRv = 0;
  const uint64_t expectedValue = getQoSClass(connectionOptions.qos);

  int val;
  socklen_t len = sizeof(expectedValue);
  int rv = getsockopt(socketFd, optkey.level, optkey.optname, &val, &len);
  if (rv != expectedRv || val != expectedValue) {
    failure::log("AsyncMcClient", failure::Category::kSystemError,
                 "Failed to apply QoS! "
                 "Return Value: {} (expected: {}). "
                 "QoS Value: {} (expected: {}).",
                 rv, expectedRv, val, expectedValue);
  }
}

apache::thrift::async::TAsyncSocket::OptionMap createSocketOptions(
    const folly::SocketAddress& address,
    const ConnectionOptions& connectionOptions) {
  apache::thrift::async::TAsyncSocket::OptionMap options;

  createTCPKeepAliveOptions(options,
    connectionOptions.tcpKeepAliveCount, connectionOptions.tcpKeepAliveIdle,
    connectionOptions.tcpKeepAliveInterval);
  if (connectionOptions.enableQoS) {
    createQoSClassOption(options, address.getFamily(), connectionOptions.qos);
  }

  return std::move(options);
}

} // anonymous namespace

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

void AsyncMcClientImpl::reply(ReqInfo::UniquePtr req, McReply mcReply) {
  idMap_.erase(req->id);
  if (req->isSync()) {
    req->syncContext.reply = std::move(mcReply);
    req->syncContext.baton.post();
  } else {
    req->asyncContext.replyCallback(std::move(mcReply));
  }
}

void AsyncMcClientImpl::replyReceived(uint64_t id, McReply mcReply) {
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
      if (req->traceCallback) {
        req->traceCallback(mcReply);
      }
      reply(std::move(req), std::move(mcReply));
    }
  } else {
    // Check that it wasn't previously replied with timeout (e.g. we hadn't
    // already removed it).
    if (pendingReplyQueue_.front().id == id) {
      auto req = pendingReplyQueue_.popFront();
      if (req->traceCallback) {
        req->traceCallback(mcReply);
      }
      reply(std::move(req), std::move(mcReply));
    }
  }
}

void AsyncMcClientImpl::attemptConnection() {
  assert(connectionState_ == ConnectionState::DOWN);

  connectionState_ = ConnectionState::CONNECTING;

  if (connectionOptions_.noNetwork) {
    socket_.reset(new MockMcClientTransport(eventBase_));
    connectSuccess();
    return;
  }

  if (connectionOptions_.sslContextProvider) {
    auto sslContext = connectionOptions_.sslContextProvider();
    if (!sslContext) {
      failure::log("AsyncMcClient", failure::Category::kBadEnvironment,
        "SSLContext provider returned nullptr, check SSL certificates. Any "
        "further request to {} will fail.",
        connectionOptions_.accessPoint.toHostPortString());
      connectError(TransportException(TransportException::SSL_ERROR));
      return;
    }
    socket_.reset(new apache::thrift::async::TAsyncSSLSocket(
      sslContext, &eventBase_));
  } else {
    socket_.reset(new apache::thrift::async::TAsyncSocket(&eventBase_));
  }

  auto& socket = dynamic_cast<apache::thrift::async::TAsyncSocket&>(*socket_);

  auto address = folly::SocketAddress(
    connectionOptions_.accessPoint.getHost().str(),
    connectionOptions_.accessPoint.getPort(),
    /* allowNameLookup */ true);

  auto socketOptions = createSocketOptions(address, connectionOptions_);

  socket.setSendTimeout(connectionOptions_.timeout.count());
  socket.connect(this, address, connectionOptions_.timeout.count(),
                 socketOptions);

  if (connectionOptions_.enableQoS) {
    checkWhetherQoSIsApplied(address, socket.getFd(), connectionOptions_);
  }
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
  parser_ = folly::make_unique<McParser>(
    static_cast<McParser::ClientParseCallback*>(this), 0,
    kReadBufferSizeMin, kReadBufferSizeMax);
  socket_->setReadCallback(this);
}

void AsyncMcClientImpl::connectError(const TransportException& ex) noexcept {
  assert(connectionState_ == ConnectionState::CONNECTING);
  DestructorGuard dg(this);

  mc_res_t error;

  if (ex.getType() == TransportException::TIMED_OUT) {
    error = mc_res_connect_timeout;
  } else if (isAborting_) {
    error = mc_res_aborted;
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

void AsyncMcClientImpl::processShutdown() {
  DestructorGuard dg(this);
  switch (connectionState_) {
    case ConnectionState::UP: // on error, UP always transitions to ERROR state
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
              McReply(isAborting_ ? mc_res_aborted : mc_res_remote_error));
      }
      if (writeQueue_.empty()) {
        // No need to send any of remaining requests if we're aborting.
        if (isAborting_) {
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
      // We shouldn't have any errors while not connected.
      CHECK(false);
  }
}

void AsyncMcClientImpl::getReadBuffer(void** bufReturn, size_t* lenReturn) {
  auto prealloc = parser_->getReadBuffer();
  *bufReturn = prealloc.first;
  *lenReturn = prealloc.second;
}

void AsyncMcClientImpl::readDataAvailable(size_t len) noexcept {
  DestructorGuard dg(this);
  parser_->readDataAvailable(len);
}

void AsyncMcClientImpl::readEOF() noexcept {
  assert(connectionState_ == ConnectionState::UP);
  processShutdown();
}

void AsyncMcClientImpl::readError(const TransportException& ex) noexcept {
  assert(connectionState_ == ConnectionState::UP);
  VLOG(1) << "Failed to read from socket with remote endpoint \""
          << connectionOptions_.accessPoint.toString()
          << "\". Exception: " << ex.what();
  processShutdown();
}

void AsyncMcClientImpl::writeSuccess() noexcept {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);
  pendingReplyQueue_.pushBack(writeQueue_.popFront());
  scheduleNextTimeout();

  // In case of no-network we need to provide fake reply.
  if (connectionOptions_.noNetwork) {
    sendFakeReply(pendingReplyQueue_.back());
  }
}

void AsyncMcClientImpl::writeError(
    size_t bytesWritten, const TransportException& ex) noexcept {

  assert(connectionState_ == ConnectionState::UP ||
         connectionState_ == ConnectionState::ERROR);

  VLOG(1) << "Failed to write into socket with remote endpoint \""
          << connectionOptions_.accessPoint.toString()
          << "\", wrote " << bytesWritten
          << " bytes. Exception: " << ex.what();

  // We're already in an error state, so all requests in pendingReplyQueue_ will
  // be replied with an error.
  pendingReplyQueue_.pushBack(writeQueue_.popFront());
  processShutdown();
}

void AsyncMcClientImpl::replyReady(McReply mcReply, mc_op_t operation,
                                   uint64_t reqId) {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);

  // Local error in ascii protocol means that there was a protocol level error,
  // e.g. we sent some command that server didn't understand. We need to log
  // the original request and close the connection.
  if (mcReply.result() == mc_res_local_error &&
      connectionOptions_.accessPoint.getProtocol() == mc_ascii_protocol) {
    if (!pendingReplyQueue_.empty()) {
      std::string requestData;
      auto& request = pendingReplyQueue_.front().reqContext;
      auto iovs = request.getIovs();
      auto iovsCount = request.getIovsCount();
      for (size_t i = 0; i < iovsCount; ++i) {
        requestData += folly::cEscape<std::string>(
          folly::StringPiece(
            reinterpret_cast<const char*>(iovs[i].iov_base), iovs[i].iov_len));
      }
      failure::log("AsyncMcClient", failure::Category::kOther,
                   "Received ERROR reply from server, original request was: "
                   "\"{}\" and the operation was {}", requestData,
                   mc_op_to_string(pendingReplyQueue_.front().op));
    } else {
      failure::log("AsyncMcClient", failure::Category::kOther,
                   "Received ERROR reply from server, but there're no "
                   "outstanding requests.");
    }
    processShutdown();
    return;
  }

  if (!outOfOrder_) {
    reqId = nextInflightMsgId_;
    incMsgId(nextInflightMsgId_);
  }
  replyReceived(reqId, std::move(mcReply));
}

void AsyncMcClientImpl::parseError(McReply errorReply) {
  // mc_parser can call the parseError multiple times, process only first.
  if (connectionState_ != ConnectionState::UP) {
    return;
  }
  DestructorGuard dg(this);
  processShutdown();
}

void AsyncMcClientImpl::sendFakeReply(ReqInfo& request) {
  auto& transport = dynamic_cast<MockMcClientTransport&>(*socket_);
  switch (request.op) {
    case mc_op_delete: {
      static const char* msg = "DELETED\r\n";
      static auto msgLen = strlen(msg);
      transport.fakeDataRead(msg, msgLen);
      break;
    }
    case mc_op_get:
    case mc_op_lease_get: {
      static const char* msg = "VALUE we:always:ignore:key:here 0 15\r\n"
                               "veryRandomValue\r\nEND\r\n";
      static auto msgLen = strlen(msg);
      transport.fakeDataRead(msg, msgLen);
      break;
    }
    case mc_op_set:
    case mc_op_lease_set: {
      static const char* msg = "STORED\r\n";
      static auto msgLen = strlen(msg);
      transport.fakeDataRead(msg, msgLen);
      break;
    }
    default: {
      static const char* msg = "CLIENT_ERROR unsupported operation\r\n";
      static auto msgLen = strlen(msg);
      transport.fakeDataRead(msg, msgLen);
    }
  }
}

void AsyncMcClientImpl::incMsgId(size_t& msgId) {
  ++msgId;
  if (UNLIKELY(msgId == 0)) {
    msgId = 1;
  }
}

}} // facebook::memcache
