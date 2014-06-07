/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyConfigIf;
class ProxyMcRequest;
class proxy_t;

/**
 * Answers mc_op_get_service_info requests of the form
 * __mcrouter__.commands(args,...)
 */
class ServiceInfo {
 public:
  ServiceInfo(proxy_t* proxy, const ProxyConfigIf& config);

  void handleRequest(const ProxyMcRequest& req) const;

  ~ServiceInfo();

 private:
  class ServiceInfoImpl;
  std::unique_ptr<ServiceInfoImpl> impl_;
};

}}}  // facebook::memcache::mcrouter
