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

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/SecurityOptions.h"

using apache::thrift::async::TAsyncSocket;

namespace facebook {
namespace memcache {

ThriftTransportBase::ThriftTransportBase(
    folly::VirtualEventBase& eventBase,
    ConnectionOptions options)
    : eventBase_(eventBase.getEventBase()),
      connectionOptions_(std::move(options)) {}

void ThriftTransportBase::closeNow() {}

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

void ThriftTransportBase::connectSuccess() noexcept {
  DestructorGuard dg(this);
  connectionState_ = ConnectionState::Up;
}

void ThriftTransportBase::connectErr(
    const folly::AsyncSocketException& ex) noexcept {
  assert(connectionState_ == ConnectionState::Connecting);
  DestructorGuard dg(this);

  LOG(ERROR) << "[ThriftClientBase] Error connecting: " << ex.what();

  connectionState_ = ConnectionState::Down;
  socket_.reset();
}

} // namespace memcache
} // namespace facebook
