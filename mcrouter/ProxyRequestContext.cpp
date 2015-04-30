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

ProxyRequestContext::ProxyRequestContext(
  proxy_t& pr,
  McMsgRef req,
  void (*enqReply)(ProxyRequestContext& preq),
  void* context,
  void (*reqComplete)(ProxyRequestContext& preq))
    : proxy_(pr),
      context_(context),
      enqueueReply_(enqReply),
      reqComplete_(reqComplete) {

  logger_.emplace(&proxy_);
  additionalLogger_.emplace(&proxy_);

  static const char* const kInternalGetPrefix = "__mcrouter__.";

  if (req->op == mc_op_get && !strncmp(req->key.str, kInternalGetPrefix,
                                       strlen(kInternalGetPrefix))) {
    /* HACK: for backwards compatibility, convert (get, "__mcrouter__.key")
       into (get-service-info, "key") */
    auto copy = MutableMcMsgRef(mc_msg_dup(req.get()));
    copy->op = mc_op_get_service_info;
    copy->key.str += strlen(kInternalGetPrefix);
    copy->key.len -= strlen(kInternalGetPrefix);
    origReq_ = std::move(copy);
  } else {
    origReq_ = std::move(req);
  }

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
    requester_->decref();
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

void ProxyRequestContext::setSenderIdForTest(uint64_t id) {
  senderIdForTest_ = id;
}

void ProxyRequestContext::onRequestRefused(const ProxyMcReply& reply) {
  if (recording_) {
    return;
  }
  assert(logger_.hasValue());
  logger_->logError(reply);
}

void ProxyRequestContext::sendReply(McReply newReply) {
  if (recording_) {
    return;
  }

  if (replied_) {
    return;
  }
  reply_ = std::move(newReply);
  replied_ = true;

  if (LIKELY(enqueueReply_ != nullptr)) {
    fiber_local::runWithoutLocals([this]() {
      enqueueReply_(*this);
    });
  }

  stat_incr(proxy_.stats, request_replied_stat, 1);
  stat_incr(proxy_.stats, request_replied_count_stat, 1);
  if (mc_res_is_err(reply_->result())) {
    stat_incr(proxy_.stats, request_error_stat, 1);
    stat_incr(proxy_.stats, request_error_count_stat, 1);
  } else {
    stat_incr(proxy_.stats, request_success_stat, 1);
    stat_incr(proxy_.stats, request_success_count_stat, 1);
  }
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
    : proxy_(pr),
      recording_(true) {
  new (&recordingState_) std::unique_ptr<RecordingState>(
    folly::make_unique<RecordingState>());
  recordingState_->clientCallback = std::move(clientCallback);
  recordingState_->shardSplitCallback = std::move(shardSplitCallback);
}

}}}
