/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/lib/network/ThriftTransport.h"

#include <folly/fibers/FiberManager.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <thrift/lib/cpp2/async/RequestChannel.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/network/AsyncTlsToPlaintextSocket.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/McFizzClient.h"
#include "mcrouter/lib/network/SecurityOptions.h"
#include "mcrouter/lib/network/SocketUtil.h"

using apache::thrift::async::TAsyncSocket;

namespace facebook {
namespace memcache {

ThriftTransportBase::ThriftTransportBase(
    folly::EventBase& eventBase,
    ConnectionOptions options)
    : eventBase_(eventBase), connectionOptions_(std::move(options)) {}

void ThriftTransportBase::closeNow() {
  resetClient();
}

void ThriftTransportBase::setConnectionStatusCallbacks(
    ConnectionStatusCallbacks callbacks) {
  connectionCallbacks_ = std::move(callbacks);

  if (connectionState_ == ConnectionState::Up && connectionCallbacks_.onUp) {
    // Connection retries not currently supported in thrift transport so pass 0
    connectionCallbacks_.onUp(*channel_->getTransport(), 0);
  }
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

apache::thrift::async::TAsyncTransport::UniquePtr
ThriftTransportBase::getConnectingSocket() {
  return folly::fibers::runInMainContext(
      [this]() -> apache::thrift::async::TAsyncTransport::UniquePtr {
        auto expectedSocket =
            createTAsyncSocket(eventBase_, connectionOptions_);
        if (expectedSocket.hasError()) {
          LOG_FAILURE(
              "ThriftTransport",
              failure::Category::kBadEnvironment,
              "{}",
              expectedSocket.error().what());
          return {};
        }
        auto socket = std::move(expectedSocket).value();

        auto sockAddressExpected = getSocketAddress(connectionOptions_);
        if (sockAddressExpected.hasError()) {
          const auto& ex = sockAddressExpected.error();
          LOG_FAILURE(
              "ThriftTransport",
              failure::Category::kBadEnvironment,
              "{}",
              ex.what());
          return {};
        }
        folly::SocketAddress address = std::move(sockAddressExpected).value();
        auto socketOptions = createSocketOptions(address, connectionOptions_);
        connectionState_ = ConnectionState::Connecting;

        const auto securityMech =
            connectionOptions_.accessPoint->getSecurityMech();
        if (securityMech == SecurityMech::TLS_TO_PLAINTEXT) {
          socket->setSendTimeout(connectionOptions_.writeTimeout.count());
          socket->getUnderlyingTransport<AsyncTlsToPlaintextSocket>()->connect(
              this,
              address,
              connectionOptions_.connectTimeout,
              std::move(socketOptions));
        } else {
          DCHECK(securityMech == SecurityMech::NONE);
          socket->setSendTimeout(connectionOptions_.writeTimeout.count());
          socket->getUnderlyingTransport<folly::AsyncSocket>()->connect(
              this,
              address,
              connectionOptions_.connectTimeout.count(),
              socketOptions);
        }
        return socket;
      });
}

apache::thrift::RocketClientChannel::Ptr ThriftTransportBase::createChannel() {
  // HHVM supports Debian 8 (EOL 2020-06-30), which includes OpenSSL 1.0.1;
  // Rocket/RSocket require ALPN, which requiers 1.0.2.
  //
  // For these platforms, build MCRouter client without a functional
  // Thrift transport, but continue to permit use as an async Memcache client
  // library for Hack
#ifndef MCROUTER_NOOP_THRIFT_CLIENT
  auto socket = getConnectingSocket();
  if (!socket) {
    return nullptr;
  }
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  channel->setProtocolId(apache::thrift::protocol::T_COMPACT_PROTOCOL);
  channel->setCloseCallback(this);
  return channel;
#else
  return nullptr;
#endif
}

apache::thrift::RpcOptions ThriftTransportBase::getRpcOptions(
    std::chrono::milliseconds timeout) {
  apache::thrift::RpcOptions rpcOptions;
  rpcOptions.setTimeout(timeout);
  rpcOptions.setClientOnlyTimeouts(true);
  return rpcOptions;
}

void ThriftTransportBase::connectSuccess() noexcept {
  assert(connectionState_ == ConnectionState::Connecting);
  connectionState_ = ConnectionState::Up;
  if (connectionCallbacks_.onUp) {
    // Connection retries not currently supported in thrift transport so pass 0
    connectionCallbacks_.onUp(*channel_->getTransport(), 0);
  }
  VLOG(5) << "[ThriftTransport] Connection successfully established!";
}

void ThriftTransportBase::connectErr(
    const folly::AsyncSocketException& ex) noexcept {
  assert(connectionState_ == ConnectionState::Connecting);

  connectionState_ = ConnectionState::Error;
  connectionTimedOut_ =
      (ex.getType() == folly::AsyncSocketException::TIMED_OUT);
  if (connectionCallbacks_.onDown) {
    ConnectionDownReason reason = ConnectionDownReason::CONNECT_ERROR;
    if (connectionTimedOut_) {
      reason = ConnectionDownReason::CONNECT_ERROR;
    }
    connectionCallbacks_.onDown(reason, 0);
  }
  VLOG(2) << "[ThriftTransport] Error connecting: " << ex.what();
}

void ThriftTransportBase::channelClosed() {
  VLOG(3) << "[ThriftTransport] Channel closed.";
  // If callbacks configured and connection up, defer reset
  // to the callback
  if (connectionCallbacks_.onDown && connectionState_ == ConnectionState::Up) {
    connectionState_ = ConnectionState::Down;
    connectionCallbacks_.onDown(ConnectionDownReason::ABORTED, 0);
  }
  resetClient();
}

} // namespace memcache
} // namespace facebook
