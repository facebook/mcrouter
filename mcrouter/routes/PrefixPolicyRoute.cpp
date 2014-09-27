/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "PrefixPolicyRoute.h"

#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makePrefixPolicyRoute(
  std::string name,
  std::vector<McrouterRouteHandlePtr> operationPolicies,
  McrouterRouteHandlePtr defaultPolicy) {

  return makeMcrouterRouteHandle<PrefixPolicyRoute>(
    std::move(name),
    std::move(operationPolicies),
    std::move(defaultPolicy));
}

McrouterRouteHandlePtr makePrefixPolicyRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<PrefixPolicyRoute>(factory, json);
}

}}}
