/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyRequestContext.h"

#include <folly/Memory.h>

#include "mcrouter/config.h"
#include "mcrouter/McrouterClient.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyRequestContext::ProxyRequestContext(proxy_t& pr,
                                         ProxyRequestPriority priority__)
    : requestId_(pr.nextRequestId()), proxy_(pr), priority_(priority__) {
  logger_.emplace(&proxy_);
  additionalLogger_.emplace(&proxy_);
  stat_incr_safe(proxy_.stats, proxy_request_num_outstanding_stat);
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
    --proxy_.numRequestsProcessing_;
    stat_decr(proxy_.stats, proxy_reqs_processing_stat, 1);
    proxy_.pump();
  }

  if (requester_) {
    if (requester_->maxOutstanding_ != 0) {
      counting_sem_post(&requester_->outstandingReqsSem_, 1);
    }
  }

  stat_decr_safe(proxy_.stats, proxy_request_num_outstanding_stat);
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
  proxy_t& proxy,
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
  proxy_t& proxy,
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
  proxy_t& pr,
  ClientCallback clientCallback,
  ShardSplitCallback shardSplitCallback)
    : requestId_(pr.nextRequestId()),
      proxy_(pr),
      recording_(true) {
  new (&recordingState_) std::unique_ptr<RecordingState>(
    folly::make_unique<RecordingState>());
  recordingState_->clientCallback = std::move(clientCallback);
  recordingState_->shardSplitCallback = std::move(shardSplitCallback);
}

}}}
