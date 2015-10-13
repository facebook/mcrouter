/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ShadowRoute.h"

#include "mcrouter/routes/DefaultShadowPolicy.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/routes/ExtraRouteHandleProviderIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShadowRouteDefault(
  McrouterRouteHandlePtr normalRoute,
  McrouterShadowData shadowData,
  DefaultShadowPolicy shadowPolicy) {

  return
    std::make_shared<McrouterRouteHandle<ShadowRoute<DefaultShadowPolicy>>>(
      std::move(normalRoute),
      std::move(shadowData),
      std::move(shadowPolicy));
}

std::vector<McrouterRouteHandlePtr> makeShadowRoutes(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json,
    std::vector<McrouterRouteHandlePtr> children,
    proxy_t& proxy,
    ExtraRouteHandleProviderIf& extraProvider) {
  folly::StringPiece shadowPolicy = "default";
  if (auto jshadow_policy = json.get_ptr("shadow_policy")) {
    checkLogic(jshadow_policy->isString(),
               "ShadowRoute: shadow_policy is not a string");
    shadowPolicy = jshadow_policy->stringPiece();
  }

  auto jshadows = json.get_ptr("shadows");
  checkLogic(jshadows, "ShadowRoute: route doesn't contain shadows field");

  if (!jshadows->isArray()) {
    MC_LOG_FAILURE(proxy.router().opts(),
                   failure::Category::kInvalidConfig,
                   "ShadowRoute: shadows specified in route is not an array");
    return children;
  }

  McrouterShadowData data;
  for (auto& shadow : *jshadows) {
    if (!shadow.isObject()) {
      MC_LOG_FAILURE(proxy.router().opts(),
                     failure::Category::kInvalidConfig,
                     "ShadowRoute: shadow is not an object");
      continue;
    }
    auto jtarget = shadow.get_ptr("target");
    if (!jtarget) {
      MC_LOG_FAILURE(proxy.router().opts(),
                     failure::Category::kInvalidConfig,
                     "ShadowRoute shadows: no target for shadow");
      continue;
    }
    try {
      auto s = ShadowSettings::create(shadow, proxy.router());
      if (s) {
        data.emplace_back(factory.create(*jtarget), std::move(s));
      }
    } catch (const std::exception& e) {
      MC_LOG_FAILURE(proxy.router().opts(),
                     failure::Category::kInvalidConfig,
                     "Can not create shadow for ShadowRoute: {}",
                     e.what());
    }
  }
  for (size_t i = 0; i < children.size(); ++i) {
    McrouterShadowData childrenShadows;
    for (const auto& shadowData : data) {
      if (shadowData.second->startIndex() <= i &&
          i < shadowData.second->endIndex()) {
        childrenShadows.push_back(shadowData);
      }
    }
    if (!childrenShadows.empty()) {
      childrenShadows.shrink_to_fit();
      children[i] = extraProvider.makeShadow(proxy,
                                                  std::move(children[i]),
                                                  std::move(childrenShadows),
                                                  shadowPolicy);
    }
  }
  return children;
}

std::vector<McrouterRouteHandlePtr> makeShadowRoutes(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json,
    proxy_t& proxy,
    ExtraRouteHandleProviderIf& extraProvider) {
  checkLogic(json.isObject(), "ShadowRoute should be an object");
  const auto jchildren = json.get_ptr("children");
  checkLogic(jchildren, "ShadowRoute: children not found");
  auto children = factory.createList(*jchildren);
  if (json.count("shadows")) {
    children = makeShadowRoutes(
        factory, json, std::move(children), proxy, extraProvider);
  }
  return children;
}

}}}
