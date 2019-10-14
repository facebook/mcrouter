/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <vector>

#include <folly/dynamic.h>

#include <mcrouter/lib/config/RouteHandleFactory.h>
#include "mcrouter/lib/test/AllSyncCollectionRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * Constructs a CollectionRoute for the given "RouterInfo".
 *
 * @param children                List of children route handles.
 */
template <class RouterInfo, template <class> class CollectionRoute>
typename RouterInfo::RouteHandlePtr createCollectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "CollectionRoute config should be an object");

  std::vector<typename RouterInfo::RouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  assert(children.size() != 0);

  if (children.size() == 1) {
    return std::move(children[0]);
  }
  return makeRouteHandleWithInfo<RouterInfo, CollectionRoute>(
      std::move(children));
}

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createAllSyncCollectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  return createCollectionRoute<RouterInfo, AllSyncCollectionRoute>(
      factory, json);
}

} // end namespace mcrouter
} // end namespace memcache
} // end namespace facebook
