/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyRequestContext.h"

#include <memory>

#include "mcrouter/CarbonRouterClientBase.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/config.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

ProxyRequestContext::ProxyRequestContext(
    ProxyBase& pr,
    ProxyRequestPriority priority__)
    : proxyBase_(pr), priority_(priority__) {
  proxyBase_.stats().incrementSafe(proxy_request_num_outstanding_stat);
}

ProxyRequestContext::~ProxyRequestContext() {
  if (recording_) {
    recordingState_.~unique_ptr<RecordingState>();
    return;
  }

  assert(replied_);

  if (processing_) {
    --proxyBase_.numRequestsProcessing_;
    proxyBase_.stats().decrement(proxy_reqs_processing_stat);
    proxyBase_.pump();
  }

  if (requester_) {
    if (requester_->maxOutstanding() != 0) {
      counting_sem_post(requester_->outstandingReqsSem(), 1);
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

void ProxyRequestContext::setSenderIdForTest(uint64_t id) {
  senderIdForTest_ = id;
}

ProxyRequestContext::ProxyRequestContext(
    RecordingT,
    ProxyBase& pr,
    ClientCallback clientCallback,
    ShardSplitCallback shardSplitCallback)
    /* pr.nextRequestId() is not threadsafe */
    : proxyBase_(pr),
      recording_(true) {
  new (&recordingState_)
      std::unique_ptr<RecordingState>(std::make_unique<RecordingState>());
  recordingState_->clientCallback = std::move(clientCallback);
  recordingState_->shardSplitCallback = std::move(shardSplitCallback);
}

} // mcrouter
} // memcache
} // facebook
