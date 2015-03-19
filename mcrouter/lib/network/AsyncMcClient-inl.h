/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>

#include "mcrouter/lib/network/AsyncMcClientImpl.h"

namespace facebook { namespace memcache {

inline AsyncMcClient::AsyncMcClient(folly::EventBase& eventBase,
                                    ConnectionOptions options) :
    base_(AsyncMcClientImpl::create(eventBase, std::move(options))) {
}

inline void AsyncMcClient::closeNow() {
  base_->closeNow();
}

inline void AsyncMcClient::setStatusCallbacks(
    std::function<void()> onUp,
    std::function<void(bool)> onDown) {
  base_->setStatusCallbacks(std::move(onUp), std::move(onDown));
}

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
AsyncMcClient::sendSync(const Request& request, Operation,
                        std::chrono::milliseconds timeout) {
  return base_->sendSync(request, Operation(), timeout);
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

inline std::pair<uint64_t, uint64_t> AsyncMcClient::getBatchingStat() const {
  return base_->getBatchingStat();
}

inline void AsyncMcClient::updateWriteTimeout(
    std::chrono::milliseconds timeout) {
  base_->updateWriteTimeout(timeout);
}

}} // facebook::memcache
