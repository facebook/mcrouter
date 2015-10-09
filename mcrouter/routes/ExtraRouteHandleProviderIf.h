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

#include <folly/Range.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/ShadowRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;

/**
 * Interface to create additional route handles for McRouteHandleProvider.
 */
class ExtraRouteHandleProviderIf {
 public:
  virtual McrouterRouteHandlePtr
  makeShadow(proxy_t& proxy,
             McrouterRouteHandlePtr destination,
             McrouterShadowData data,
             folly::StringPiece shadowPolicy) = 0;

  virtual std::vector<McrouterRouteHandlePtr>
  tryCreate(RouteHandleFactory<McrouterRouteHandleIf>& factory,
            folly::StringPiece type,
            const folly::dynamic& json) = 0;

  virtual ~ExtraRouteHandleProviderIf() {}
};

}}}  // facebook::memcache::mcrouter
