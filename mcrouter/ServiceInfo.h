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

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouterInfo>
class ProxyConfig;
template <class RouterInfo, class Request>
class ProxyRequestContextTyped;
class ProxyBase;

/**
 * Answers mc_op_get_service_info requests of the form
 * __mcrouter__.commands(args,...)
 */
template <class RouterInfo>
class ServiceInfo {
 public:
  ServiceInfo(ProxyBase* proxy, const ProxyConfig<RouterInfo>& config);

  void handleRequest(
      folly::StringPiece req,
      const std::shared_ptr<
          ProxyRequestContextTyped<RouterInfo, McGetRequest>>& ctx) const;

  ~ServiceInfo();

 private:
  struct ServiceInfoImpl;
  std::unique_ptr<ServiceInfoImpl> impl_;
};

}}}  // facebook::memcache::mcrouter

#include "ServiceInfo-inl.h"
