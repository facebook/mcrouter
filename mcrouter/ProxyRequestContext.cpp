/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyRequestContext.h"

#include "folly/Memory.h"
#include "mcrouter/config.h"
#include "mcrouter/mcrouter_client.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyRequestContext::ProxyRequestContext(proxy_request_t* preq,
                                         std::shared_ptr<ProxyRoute> pr)
    : preq_(proxy_request_incref(preq)),
      proxyRoute_(std::move(pr)) {
  FBI_ASSERT(preq_);
}

ProxyRequestContext::~ProxyRequestContext() {
  proxy_request_decref(preq_);
}

uint64_t ProxyRequestContext::senderId() const {
  uint64_t id = 0;
  if (preq_->requester) {
    id = preq_->requester->clientId;
  }

  return id;
}

proxy_request_t& ProxyRequestContext::proxyRequest() const {
  return *preq_;
}

ProxyRoute& ProxyRequestContext::proxyRoute() const {
  return *proxyRoute_;
}

}}}
