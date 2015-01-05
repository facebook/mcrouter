/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/ProxyRequestLogger.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientCommon;
class ProxyMcReply;
class ProxyMcRequest;

class LoggingProxyRequestContext {
 public:
  template <typename... Args>
  explicit LoggingProxyRequestContext(Args&&... args)
      : ctx_(std::forward<Args>(args)...),
        logger_(ctx_.proxyRequest().proxy) {
  }

  /**
   * Called once a reply is received to record a stats sample if required.
   */
  template <typename Operation>
  void onReplyReceived(const ProxyClientCommon& pclient,
                       const ProxyMcRequest& request,
                       const ProxyMcReply& reply,
                       const int64_t startTimeUs,
                       const int64_t endTimeUs,
                       Operation) {
    logger_.log(pclient, request, reply, startTimeUs, endTimeUs, Operation());
  }

  const ProxyRequestContext& ctx() const {
    return ctx_;
  }

 private:
  ProxyRequestContext ctx_;
  ProxyRequestLogger logger_;
};

}}}  // facebook::memcache::mcrouter
