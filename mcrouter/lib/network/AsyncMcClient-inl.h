/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
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
    std::function<void(const folly::AsyncSocket&)> onUp,
    std::function<void(ConnectionDownReason)> onDown) {
  base_->setStatusCallbacks(std::move(onUp), std::move(onDown));
}

inline void AsyncMcClient::setRequestStatusCallbacks(
    std::function<void(int pendingDiff, int inflightDiff)> onStateChange,
    std::function<void(int numToSend)> onWrite) {
  base_->setRequestStatusCallbacks(
      std::move(onStateChange), std::move(onWrite));
}

template <class Request>
ReplyT<Request> AsyncMcClient::sendSync(
    const Request& request,
    std::chrono::milliseconds timeout,
    ReplyStatsContext* replyContext) {
  return base_->sendSync(request, timeout, replyContext);
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

inline void AsyncMcClient::updateWriteTimeout(
    std::chrono::milliseconds timeout) {
  base_->updateWriteTimeout(timeout);
}

inline const folly::AsyncTransportWrapper* AsyncMcClient::getTransport() {
  return base_->getTransport();
}

inline double AsyncMcClient::getRetransmissionInfo() {
  return base_->getRetransmissionInfo();
}

template <class Request>
double AsyncMcClient::getDropProbability() const {
  return base_->getDropProbability<Request>();
}
} // memcache
} // facebook
