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
      reqComplete_(reqComplete),
      logger_(&pr),
      additionalLogger_(&pr) {

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
  assert(replied_);
  if (reqComplete_) {
    reqComplete_(*this);
  }

  if (processing_) {
    --proxy_.numRequestsProcessing_;
    stat_decr(proxy_.stats, proxy_reqs_processing_stat, 1);
    proxy_.pump();
  }

  if (requester_) {
    requester_->decref();
  }

  stat_decr_safe(proxy_.stats, proxy_request_num_outstanding_stat);
}

uint64_t ProxyRequestContext::senderId() const {
  uint64_t id = 0;
  if (requester_) {
    id = requester_->clientId();
  }

  return id;
}

void ProxyRequestContext::onRequestRefused(const ProxyMcRequest& request,
                                           const ProxyMcReply& reply) {
  logger_.logError(request, reply);
}

void ProxyRequestContext::sendReply(McReply newReply) {
  if (replied_) {
    return;
  }
  reply_ = std::move(newReply);
  replied_ = true;

  if (LIKELY(enqueueReply_ != nullptr)) {
    enqueueReply_(*this);
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

}}}
