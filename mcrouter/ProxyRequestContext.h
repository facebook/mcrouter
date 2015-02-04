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

#include <memory>

#include "mcrouter/config.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/ProxyConfigIf.h"
#include "mcrouter/ProxyRequestLogger.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientCommon;
class ProxyMcReply;
class ProxyMcRequest;
class ProxyRoute;
class proxy_request_t;

/**
 * Links lifetime of proxy_request_t with a McRequest context object
 *
 * This is for the transition only. Later on, proxy_request_t
 * will transform into something that can completely replace this.
 */
class ProxyRequestContext {
 public:
  ProxyRequestContext(proxy_request_t* preq,
                      std::shared_ptr<const ProxyConfigIf> config);

  ~ProxyRequestContext();

  uint64_t senderId() const;

  proxy_request_t& proxyRequest() const {
    return *preq_;
  }

  ProxyRoute& proxyRoute() const {
    return config_->proxyRoute();
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
    additionalLogger_.log(
      pclient, request, reply, startTimeUs, endTimeUs, Operation());
  }


 private:
  proxy_request_t* preq_;
  std::shared_ptr<const ProxyConfigIf> config_;

  ProxyRequestLogger logger_;
  AdditionalProxyRequestLogger additionalLogger_;
};

}}}  // facebook::memcache::mcrouter
