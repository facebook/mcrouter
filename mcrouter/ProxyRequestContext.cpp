/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyRequestContext.h"

#include <folly/Memory.h>

#include "mcrouter/McrouterClient.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/config.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyRequestContext::ProxyRequestContext(
    ProxyBase& pr,
    ProxyRequestPriority priority__)
    : requestId_(pr.nextRequestId()), proxyBase_(pr), priority_(priority__) {
  logger_.emplace(&proxyBase_);
  additionalLogger_.emplace(&proxyBase_);
  proxyBase_.stats().incrementSafe(proxy_request_num_outstanding_stat);
}

ProxyRequestContext::~ProxyRequestContext() {
  if (recording_) {
    recordingState_.~unique_ptr<RecordingState>();
    return;
  }

  assert(replied_);
  if (reqComplete_) {
    fiber_local::runWithoutLocals([this]() {
      reqComplete_(*this);
    });
  }

  if (processing_) {
    --proxyBase_.numRequestsProcessing_;
    proxyBase_.stats().decrement(proxy_reqs_processing_stat);
    proxyBase_.pump();
  }

  if (requester_) {
    if (requester_->maxOutstanding_ != 0) {
      counting_sem_post(&requester_->outstandingReqsSem_, 1);
    }
  }

  proxyBase_.stats().decrementSafe(proxy_request_num_outstanding_stat);
}

uint64_t ProxyRequestContext::senderId() const {
  uint64_t id = 0;
  if (requester_) {
    id = requester_->clientId();
  } else {
    id = senderIdForTest_;
  }

  return id;
}

uint64_t ProxyRequestContext::requestId() const {
  return requestId_;
}

void ProxyRequestContext::setSenderIdForTest(uint64_t id) {
  senderIdForTest_ = id;
}

std::shared_ptr<ProxyRequestContext> ProxyRequestContext::createRecording(
    ProxyBase& proxy,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback) {
  return std::shared_ptr<ProxyRequestContext>(
    new ProxyRequestContext(Recording,
                            proxy,
                            std::move(clientCallback),
                            std::move(shardSplitCallback))
  );
}

std::shared_ptr<ProxyRequestContext> ProxyRequestContext::createRecordingNotify(
    ProxyBase& proxy,
    folly::fibers::Baton& baton,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback) {
  return std::shared_ptr<ProxyRequestContext>(
    new ProxyRequestContext(Recording,
                            proxy,
                            std::move(clientCallback),
                            std::move(shardSplitCallback)),
    [&baton] (ProxyRequestContext* ctx) {
      delete ctx;
      baton.post();
    });
}

ProxyRequestContext::ProxyRequestContext(
    RecordingT,
    ProxyBase& pr,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback)
    /* pr.nextRequestId() is not threadsafe */
    : requestId_(0), proxyBase_(pr), recording_(true) {
  new (&recordingState_) std::unique_ptr<RecordingState>(
    folly::make_unique<RecordingState>());
  recordingState_->clientCallback = std::move(clientCallback);
  recordingState_->shardSplitCallback = std::move(shardSplitCallback);
}

}}} // facebook::memcache::mcrouter
