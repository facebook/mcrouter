/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ShardSplitRoute.h"

#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShardSplitRoute(
  McrouterRouteHandlePtr rh,
  ShardSplitter shardSplitter) {

  return makeMcrouterRouteHandle<ShardSplitRoute>(
    std::move(rh), std::move(shardSplitter));
}

}}}
