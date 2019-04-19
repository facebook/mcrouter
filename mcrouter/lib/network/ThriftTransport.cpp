/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "mcrouter/lib/network/ThriftTransport.h"

#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/VirtualEventBase.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <thrift/lib/cpp2/async/RequestChannel.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/McFizzClient.h"
#include "mcrouter/lib/network/SecurityOptions.h"
#include "mcrouter/lib/network/SocketUtil.h"

using apache::thrift::async::TAsyncSocket;

namespace facebook {
namespace memcache {

ThriftTransportBase::ThriftTransportBase(
    folly::VirtualEventBase& eventBase,
    ConnectionOptions options)
    : eventBase_(eventBase.getEventBase()),
      connectionOptions_(std::move(options)) {}

void ThriftTransportBase::closeNow() {
  resetClient();
}

void ThriftTransportBase::setConnectionStatusCallbacks(
    ConnectionStatusCallbacks callbacks) {
  connectionCallbacks_ = std::move(callbacks);
}

void ThriftTransportBase::setRequestStatusCallbacks(
    RequestStatusCallbacks callbacks) {
  requestCallbacks_ = std::move(callbacks);
}

void ThriftTransportBase::setThrottle(size_t maxInflight, size_t maxPending) {
  maxInflight_ = maxInflight;
  maxPending_ = maxPending;
}

Transport::RequestQueueStats ThriftTransportBase::getRequestQueueStats() const {
  return RequestQueueStats{0, 0};
}

void ThriftTransportBase::updateTimeoutsIfShorter(
    std::chrono::milliseconds /* connectTimeout */,
    std::chrono::milliseconds /* writeTimeout */) {}

const folly::AsyncTransportWrapper* ThriftTransportBase::getTransport() const {
  return nullptr;
}

double ThriftTransportBase::getRetransmitsPerKb() {
  return 0.0;
}

void ThriftTransportBase::setFlushList(FlushList* /* flushList */) {}

apache::thrift::async::TAsyncTransport::UniquePtr
ThriftTransportBase::getConnectingSocket() {
  return folly::fibers::runInMainContext([this] {
    // TODO(@aap): Replace with createSocket() once Thrift works with
    // AsyncTransportWrapper.
    apache::thrift::async::TAsyncTransport::UniquePtr socket(
        new apache::thrift::async::TAsyncSocket(&eventBase_));

    socket->setSendTimeout(connectionOptions_.writeTimeout.count());

    auto sockAddressExpected = getSocketAddress(connectionOptions_);
    if (sockAddressExpected.hasError()) {
      const auto& ex = sockAddressExpected.error();
      LOG_FAILURE(
          "ThriftTransport",
          failure::Category::kBadEnvironment,
          "{}",
          ex.what());
      return apache::thrift::async::TAsyncTransport::UniquePtr{};
    }
    folly::SocketAddress address = std::move(sockAddressExpected).value();

    auto socketOptions = createSocketOptions(address, connectionOptions_);
    const auto mech = connectionOptions_.accessPoint->getSecurityMech();
    connectionState_ = ConnectionState::Connecting;
    if (mech == SecurityMech::TLS13_FIZZ) {
      auto fizzClient = socket->getUnderlyingTransport<McFizzClient>();
      fizzClient->connect(
          this,
          address,
          connectionOptions_.connectTimeout.count(),
          socketOptions);
    } else {
      auto asyncSock = socket->getUnderlyingTransport<folly::AsyncSocket>();
      asyncSock->connect(
          this,
          address,
          connectionOptions_.connectTimeout.count(),
          socketOptions);
    }
    return socket;
  });
}

apache::thrift::RocketClientChannel::Ptr ThriftTransportBase::createChannel() {
  auto socket = getConnectingSocket();
  if (!socket) {
    return nullptr;
  }
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  channel->setCloseCallback(this);
  return channel;
}

apache::thrift::RpcOptions ThriftTransportBase::getRpcOptions(
    std::chrono::milliseconds timeout) const {
  apache::thrift::RpcOptions rpcOptions;
  rpcOptions.setTimeout(timeout);
  return rpcOptions;
}

void ThriftTransportBase::connectSuccess() noexcept {
  assert(connectionState_ == ConnectionState::Connecting);
  connectionState_ = ConnectionState::Up;
  VLOG(5) << "[ThriftTransport] Connection successfully established!";
}

void ThriftTransportBase::connectErr(
    const folly::AsyncSocketException& ex) noexcept {
  assert(connectionState_ == ConnectionState::Connecting);

  connectionState_ = ConnectionState::Error;
  connectionTimedOut_ =
      (ex.getType() == folly::AsyncSocketException::TIMED_OUT);

  VLOG(2) << "[ThriftTransport] Error connecting: " << ex.what();
}

void ThriftTransportBase::channelClosed() {
  VLOG(3) << "[ThriftTransport] Channel closed.";
  resetClient();
}

} // namespace memcache
} // namespace facebook
