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
#include <string>
#include <unordered_map>
#include <vector>

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

template <class RouteHandleIf>
class RouteHandleProviderIf;

/**
 * Parses RouteHandle tree from JSON object.
 */
template <class RouteHandleIf>
class RouteHandleFactory {
 public:
  RouteHandleFactory(const RouteHandleFactory&) = delete;
  RouteHandleFactory& operator=(const RouteHandleFactory&) = delete;

  /**
   * @param provider that can create single node of RouteHandle tree.
   */
  explicit RouteHandleFactory(RouteHandleProviderIf<RouteHandleIf>& provider);

  /**
   * Creates single RouteHandle from JSON object.
   *
   * @param json object that contains RouteHandle with (optional) children.
   */
  std::shared_ptr<RouteHandleIf> create(const folly::dynamic& json);

  /**
   * Creates multiple subtrees from JSON object. Should be used to create
   * children of some RouteHandle.
   *
   * @param json array, object or string that represents zero, one or multiple
   *             RouteHandles.
   */
  std::vector<std::shared_ptr<RouteHandleIf>>
  createList(const folly::dynamic& json);
 private:
  RouteHandleProviderIf<RouteHandleIf>& provider_;

  /// Named routes we've already parsed
  std::unordered_map<std::string,
                     std::vector<std::shared_ptr<RouteHandleIf>>> seen_;

};

}} // facebook::memcache

#include "RouteHandleFactory-inl.h"
