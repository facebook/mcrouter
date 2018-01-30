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

#include <memory>

#include <folly/Range.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class Proxy;
template <class RouterInfo>
class ProxyConfig;
template <class RouterInfo, class Request>
class ProxyRequestContextTyped;

using ServiceInfoRequest = McGetRequest;

/**
 * Answers mc_op_get_service_info requests of the form
 * __mcrouter__.commands(args,...)
 */
template <class RouterInfo>
class ServiceInfo {
 public:
  ServiceInfo(Proxy<RouterInfo>& proxy, const ProxyConfig<RouterInfo>& config);

  void handleRequest(
      folly::StringPiece req,
      const std::shared_ptr<
          ProxyRequestContextTyped<RouterInfo, ServiceInfoRequest>>& ctx) const;

  ~ServiceInfo();

 private:
  struct ServiceInfoImpl;
  std::unique_ptr<ServiceInfoImpl> impl_;
};

} // mcrouter
} // memcache
} // facebook

#include "ServiceInfo-inl.h"
