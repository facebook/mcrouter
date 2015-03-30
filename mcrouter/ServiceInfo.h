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

#include <folly/Range.h>

#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

class McRequest;

namespace mcrouter {

class ProxyConfig;
template <class Operation, class Request>
class ProxyRequestContextTyped;
class proxy_t;

/**
 * Answers mc_op_get_service_info requests of the form
 * __mcrouter__.commands(args,...)
 */
class ServiceInfo {
 public:
  using ContextType =
      ProxyRequestContextTyped<McOperation<mc_op_get>, McRequest>;
  ServiceInfo(proxy_t* proxy, const ProxyConfig& config);

  void handleRequest(folly::StringPiece req,
                     const std::shared_ptr<ContextType>& ctx) const;
  ~ServiceInfo();

 private:
  class ServiceInfoImpl;
  std::unique_ptr<ServiceInfoImpl> impl_;
};

}}}  // facebook::memcache::mcrouter
