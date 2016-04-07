/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyConfig;
template <class Request>
class ProxyRequestContextTyped;
struct proxy_t;

/**
 * Answers mc_op_get_service_info requests of the form
 * __mcrouter__.commands(args,...)
 */
class ServiceInfo {
 public:
  ServiceInfo(proxy_t* proxy, const ProxyConfig& config);

  void handleRequest(
      folly::StringPiece req,
      const std::shared_ptr<ProxyRequestContextTyped<
          McRequestWithMcOp<mc_op_get>>>& ctx) const;

  void handleRequest(
      folly::StringPiece req,
      const std::shared_ptr<ProxyRequestContextTyped<
          TypedThriftRequest<cpp2::McGetRequest>>>& ctx) const;

  ~ServiceInfo();

 private:
  class ServiceInfoImpl;
  std::unique_ptr<ServiceInfoImpl> impl_;
};

}}}  // facebook::memcache::mcrouter
