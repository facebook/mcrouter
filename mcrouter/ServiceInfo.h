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

namespace facebook { namespace memcache {

class McRequest;

namespace mcrouter {

class ProxyConfigIf;
class ProxyRequestContext;
class proxy_t;

/**
 * Answers mc_op_get_service_info requests of the form
 * __mcrouter__.commands(args,...)
 */
class ServiceInfo {
 public:
  ServiceInfo(proxy_t* proxy, const ProxyConfigIf& config);

  void handleRequest(const McRequest& req,
                     const std::shared_ptr<ProxyRequestContext>& ctx) const;

  ~ServiceInfo();

 private:
  class ServiceInfoImpl;
  std::unique_ptr<ServiceInfoImpl> impl_;
};

}}}  // facebook::memcache::mcrouter
