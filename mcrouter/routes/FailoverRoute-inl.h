/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeFailoverRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    ExtraRouteHandleProviderIf<RouterInfo>& extraProvider) {
  std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return extraProvider.makeFailoverRoute(json, std::move(children));
}

template <
    class RouterInfo,
    template <typename...> class RouteHandle,
    class... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeFailoverRouteInOrder(
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> rh,
    Args&&... args) {
  if (rh.size() <= 1) {
    return makeNullOrSingletonRoute(std::move(rh));
  }

  using FailoverPolicyT =
      FailoverInOrderPolicy<typename RouterInfo::RouteHandleIf>;
  return makeRouteHandleWithInfo<RouterInfo, RouteHandle, FailoverPolicyT>(
      std::move(rh), std::forward<Args>(args)...);
}

template <
    class RouterInfo,
    template <class...> class RouteHandle,
    class... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf>
makeFailoverRouteLeastFailures(
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> rh,
    Args&&... args) {
  if (rh.size() <= 1) {
    return makeNullOrSingletonRoute(std::move(rh));
  }

  using FailoverPolicyT =
      FailoverLeastFailuresPolicy<typename RouterInfo::RouteHandleIf>;
  return makeRouteHandleWithInfo<RouterInfo, RouteHandle, FailoverPolicyT>(
      std::move(rh), std::forward<Args>(args)...);
}

template <
    class RouterInfo,
    template <class...> class RouteHandle,
    typename FailoverPolicyT,
    class FailoverErrorsSettingsT,
    class... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf>
makeFailoverRouteWithPolicyAndFailoverError(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children,
    const folly::dynamic& policyConfig,
    FailoverErrorsSettingsT failoverErrors,
    Args&&... args) {
  if (children.size() <= 1 && FailoverPolicyT::optimizeNoFailoverRouteCase) {
    return makeNullOrSingletonRoute(std::move(children));
  }

  std::unique_ptr<FailoverRateLimiter> rateLimiter;
  bool failoverTagging = false;
  bool enableLeasePairing = false;
  std::string name;
  if (json.isObject()) {
    if (auto jLeasePairing = json.get_ptr("enable_lease_pairing")) {
      checkLogic(
          jLeasePairing->isBool(),
          "Failover: enable_lease_pairing is not bool");
      enableLeasePairing = jLeasePairing->getBool();
    }
    if (auto jName = json.get_ptr("name")) {
      checkLogic(jName->isString(), "Failover: name is not a string");
      name = jName->getString();
    } else {
      checkLogic(
          !enableLeasePairing,
          "Failover: name is required when lease pairing is enabled");
    }
    if (auto jFailoverTag = json.get_ptr("failover_tag")) {
      checkLogic(jFailoverTag->isBool(), "Failover: failover_tag is not bool");
      failoverTagging = jFailoverTag->getBool();
    }
    if (auto jFailoverLimit = json.get_ptr("failover_limit")) {
      rateLimiter = std::make_unique<FailoverRateLimiter>(*jFailoverLimit);
    }
  }

  return makeRouteHandleWithInfo<
      RouterInfo,
      RouteHandle,
      FailoverPolicyT,
      FailoverErrorsSettingsT>(
      std::move(children),
      std::move(failoverErrors),
      std::move(rateLimiter),
      failoverTagging,
      enableLeasePairing,
      std::move(name),
      policyConfig,
      std::forward<Args>(args)...);
}

template <
    class RouterInfo,
    template <class...> class RouteHandle,
    typename FailoverPolicyT,
    class... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeFailoverRouteWithPolicy(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children,
    const folly::dynamic& policyConfig,
    Args&&... args) {
  FailoverErrorsSettings failoverErrors;
  if (json.isObject()) {
    if (auto jFailoverErrors = json.get_ptr("failover_errors")) {
      failoverErrors = FailoverErrorsSettings(*jFailoverErrors);
    }
  }
  return makeFailoverRouteWithPolicyAndFailoverError<
      RouterInfo,
      RouteHandle,
      FailoverPolicyT,
      FailoverErrorsSettings>(
      json,
      children,
      policyConfig,
      std::move(failoverErrors),
      std::forward<Args>(args)...);
}

template <
    class RouterInfo,
    template <class...> class RouteHandle,
    class FailoverErrorsSettingsT,
    class... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf>
makeFailoverRouteWithFailoverErrorSettings(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children,
    FailoverErrorsSettingsT failoverErrors,
    Args&&... args) {
  if (json.isObject()) {
    if (auto jFailoverPolicy = json.get_ptr("failover_policy")) {
      checkLogic(
          jFailoverPolicy->isObject(),
          "Failover: failover_policy is not object");
      auto jPolicyType = jFailoverPolicy->get_ptr("type");
      checkLogic(
          jPolicyType != nullptr,
          "Failover: failover_policy object is missing 'type' field");
      if (parseString(*jPolicyType, "type") == "LeastFailuresPolicy") {
        using FailoverPolicyT =
            FailoverLeastFailuresPolicy<typename RouterInfo::RouteHandleIf>;
        return makeFailoverRouteWithPolicyAndFailoverError<
            RouterInfo,
            RouteHandle,
            FailoverPolicyT,
            FailoverErrorsSettingsT>(
            json,
            std::move(children),
            *jFailoverPolicy,
            std::move(failoverErrors),
            std::forward<Args>(args)...);
      }
    }
  }
  using FailoverPolicyT =
      FailoverInOrderPolicy<typename RouterInfo::RouteHandleIf>;
  return makeFailoverRouteWithPolicyAndFailoverError<
      RouterInfo,
      RouteHandle,
      FailoverPolicyT,
      FailoverErrorsSettingsT>(
      json,
      std::move(children),
      nullptr,
      std::move(failoverErrors),
      std::forward<Args>(args)...);
}

template <
    class RouterInfo,
    template <class...> class RouteHandle,
    class... Args>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeFailoverRouteDefault(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children,
    Args&&... args) {
  FailoverErrorsSettings failoverErrors;
  if (json.isObject()) {
    if (auto jFailoverErrors = json.get_ptr("failover_errors")) {
      failoverErrors = FailoverErrorsSettings(*jFailoverErrors);
    }
  }
  return makeFailoverRouteWithFailoverErrorSettings<
      RouterInfo,
      RouteHandle,
      FailoverErrorsSettings>(
      json,
      std::move(children),
      std::move(failoverErrors),
      std::forward<Args>(args)...);
}
} // mcrouter
} // memcache
} // facebook
