/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class Proxy;
struct RequestLoggerContext;

template <class RouterInfo>
class ProxyRequestLogger {
 public:
  explicit ProxyRequestLogger(Proxy<RouterInfo>& proxy) : proxy_(proxy) {}

  template <class Request>
  void log(const RequestLoggerContext& loggerContext);

 protected:
  Proxy<RouterInfo>& proxy_;

  void logError(mc_res_t result, RequestClass reqClass);
};
}
}
} // facebook::memcache::mcrouter

#include "ProxyRequestLogger-inl.h"
