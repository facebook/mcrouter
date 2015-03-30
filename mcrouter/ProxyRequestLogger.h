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

#include "mcrouter/lib/Operation.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class proxy_t;

class ProxyRequestLogger {
 public:
  explicit ProxyRequestLogger(proxy_t* proxy)
    : proxy_(proxy) {
  }

  template <class Operation, class Request>
  void log(const Request& request,
           const ReplyT<Operation, Request>& reply,
           const int64_t startTimeUs,
           const int64_t endTimeUs,
           Operation);

 protected:
  proxy_t* proxy_;

  template <class Reply>
  inline void logError(const Reply& reply);
};

}}}  // facebook::memcache::mcrouter

#include "ProxyRequestLogger-inl.h"
