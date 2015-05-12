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

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShadowRouteDefault(
  McrouterRouteHandlePtr normalRoute,
  std::shared_ptr<McrouterShadowData> shadowData,
  size_t normalIndex,
  DefaultShadowPolicy shadowPolicy) {

  return
    std::make_shared<McrouterRouteHandle<ShadowRoute<DefaultShadowPolicy>>>(
      std::move(normalRoute),
      std::move(shadowData),
      normalIndex,
      std::move(shadowPolicy));
}

}}}
