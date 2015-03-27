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

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;
class ProxyMcReply;
class ProxyMcRequest;

class ProxyRequestLogger {
 public:
  explicit ProxyRequestLogger(proxy_t* proxy)
    : proxy_(proxy) {
  }

  template <class Operation>
  void log(const ProxyMcRequest& request,
           const ProxyMcReply& reply,
           const int64_t startTimeUs,
           const int64_t endTimeUs,
           Operation);

  inline void logError(const ProxyMcRequest& request, const McReplyBase& reply);

 protected:
  proxy_t* proxy_;
};

}}}  // facebook::memcache::mcrouter

#include "ProxyRequestLogger-inl.h"
