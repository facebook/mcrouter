/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FailoverRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/FailoverPolicy.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr
makeNullOrSingletonRoute(std::vector<McrouterRouteHandlePtr> rh) {
  assert(rh.size() <= 1);
  if (rh.empty()) {
    return makeNullRoute();
  }
  return std::move(rh[0]);
}

McrouterRouteHandlePtr
makeFailoverRouteInOrder(std::vector<McrouterRouteHandlePtr> rh,
                         FailoverErrorsSettings failoverErrors,
                         std::unique_ptr<FailoverRateLimiter> rateLimiter,
                         bool failoverTagging,
                         bool enableLeasePairing,
                         std::string name) {
  if (rh.size() <= 1) {
    return makeNullOrSingletonRoute(std::move(rh));
  }

  using FailoverPolicyT = FailoverInOrderPolicy<McrouterRouteHandleIf>;
  return makeMcrouterRouteHandle<FailoverRoute, FailoverPolicyT>(
      std::move(rh),
      std::move(failoverErrors),
      std::move(rateLimiter),
      failoverTagging,
      enableLeasePairing,
      std::move(name));
}

McrouterRouteHandlePtr
makeFailoverRouteLeastFailures(std::vector<McrouterRouteHandlePtr> rh,
                               FailoverErrorsSettings failoverErrors,
                               std::unique_ptr<FailoverRateLimiter> rateLimiter,
                               bool failoverTagging,
                               bool enableLeasePairing,
                               std::string name,
                               const folly::dynamic& json) {
  if (rh.size() <= 1) {
    return makeNullOrSingletonRoute(std::move(rh));
  }

  using FailoverPolicyT = FailoverLeastFailuresPolicy<McrouterRouteHandleIf>;
  return makeMcrouterRouteHandle<FailoverRoute, FailoverPolicyT>(
      std::move(rh),
      std::move(failoverErrors),
      std::move(rateLimiter),
      failoverTagging,
      enableLeasePairing,
      std::move(name),
      json);
}

McrouterRouteHandlePtr makeFailoverRoute(
    const folly::dynamic& json,
    std::vector<McrouterRouteHandlePtr> children) {

  FailoverErrorsSettings failoverErrors;
  std::unique_ptr<FailoverRateLimiter> rateLimiter;
  bool failoverTagging = false;
  bool enableLeasePairing = false;
  std::string name;
  if (json.isObject()) {
    if (auto jLeasePairing = json.get_ptr("enable_lease_pairing")) {
      checkLogic(jLeasePairing->isBool(),
                 "Failover: enable_lease_pairing is not bool");
      enableLeasePairing = jLeasePairing->getBool();
    }
    if (auto jName = json.get_ptr("name")) {
      checkLogic(jName->isString(), "Failover: name is not a string");
      name = jName->getString();
    } else {
      checkLogic(!enableLeasePairing,
                 "Failover: name is required when lease pairing is enabled");
    }
    if (auto jFailoverErrors = json.get_ptr("failover_errors")) {
      failoverErrors = FailoverErrorsSettings(*jFailoverErrors);
    }
    if (auto jFailoverTag = json.get_ptr("failover_tag")) {
      checkLogic(jFailoverTag->isBool(),
                 "Failover: failover_tag is not bool");
      failoverTagging = jFailoverTag->getBool();
    }
    if (auto jFailoverLimit = json.get_ptr("failover_limit")) {
      rateLimiter = folly::make_unique<FailoverRateLimiter>(*jFailoverLimit);
    }
    if (auto jFailoverPolicy = json.get_ptr("failover_policy")) {
      checkLogic(jFailoverPolicy->isObject(),
                 "Failover: failover_policy is not object");
      auto jPolicyType = jFailoverPolicy->get_ptr("type");
      checkLogic(jPolicyType != nullptr,
                 "Failover: failover_policy object is missing 'type' field");
      if (parseString(*jPolicyType, "type") == "LeastFailuresPolicy") {
        return makeFailoverRouteLeastFailures(
            std::move(children), std::move(failoverErrors),
            std::move(rateLimiter), failoverTagging,
            enableLeasePairing, std::move(name),
            *jFailoverPolicy);
      }
    }
  }
  return makeFailoverRouteInOrder(
      std::move(children), std::move(failoverErrors),
      std::move(rateLimiter), failoverTagging,
      enableLeasePairing, std::move(name));
}

McrouterRouteHandlePtr makeFailoverRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<McrouterRouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return makeFailoverRoute(json, std::move(children));
}

}}}  // facebook::memcache::mcrouter
