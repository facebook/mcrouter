/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/routes/ExtraRouteHandleProviderIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;

/**
 * Creates additional route handles for McRouteHandleProvider.
 */
class McExtraRouteHandleProvider : public ExtraRouteHandleProviderIf {
 public:
  virtual McrouterRouteHandlePtr
  makeShadow(proxy_t* proxy,
             McrouterRouteHandlePtr destination,
             const McrouterShadowData& data,
             size_t indexInPool,
             folly::StringPiece shadowPolicy) override;

  virtual ~McExtraRouteHandleProvider() {}
};

}}}  // facebook::memcache::mcrouter
