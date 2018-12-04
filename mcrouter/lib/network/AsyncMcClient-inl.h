/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
namespace facebook {
namespace memcache {

inline AsyncMcClient::AsyncMcClient(
    folly::EventBase& eventBase,
    ConnectionOptions options)
    : AsyncMcClient(eventBase.getVirtualEventBase(), options) {}

inline AsyncMcClient::AsyncMcClient(
    folly::VirtualEventBase& eventBase,
    ConnectionOptions options)
    : base_(AsyncMcClientImpl::create(eventBase, std::move(options))) {}

inline void AsyncMcClient::closeNow() {
  base_->closeNow();
}

inline void AsyncMcClient::setStatusCallbacks(
    std::function<void(const folly::AsyncTransportWrapper&, int64_t)> onUp,
    std::function<void(ConnectionDownReason, int64_t)> onDown) {
  base_->setStatusCallbacks(std::move(onUp), std::move(onDown));
}

inline void AsyncMcClient::setRequestStatusCallbacks(
    std::function<void(int pendingDiff, int inflightDiff)> onStateChange,
    std::function<void(size_t numToSend)> onWrite,
    std::function<void()> onPartialWrite) {
  base_->setRequestStatusCallbacks(
      std::move(onStateChange), std::move(onWrite), std::move(onPartialWrite));
}

template <class Request>
ReplyT<Request> AsyncMcClient::sendSync(
    const Request& request,
    std::chrono::milliseconds timeout,
    RpcStatsContext* rpcContext) {
  return base_->sendSync(request, timeout, rpcContext);
}

inline void AsyncMcClient::setThrottle(size_t maxInflight, size_t maxPending) {
  base_->setThrottle(maxInflight, maxPending);
}

inline size_t AsyncMcClient::getPendingRequestCount() const {
  return base_->getPendingRequestCount();
}

inline size_t AsyncMcClient::getInflightRequestCount() const {
  return base_->getInflightRequestCount();
}

inline void AsyncMcClient::updateTimeoutsIfShorter(
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds writeTimeout) {
  base_->updateTimeoutsIfShorter(connectTimeout, writeTimeout);
}

inline const folly::AsyncTransportWrapper* AsyncMcClient::getTransport() {
  return base_->getTransport();
}

inline double AsyncMcClient::getRetransmissionInfo() {
  return base_->getRetransmissionInfo();
}
} // memcache
} // facebook
