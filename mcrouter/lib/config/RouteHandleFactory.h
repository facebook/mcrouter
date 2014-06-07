/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

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
  typedef std::function<
    std::shared_ptr<RouteHandleIf>(std::shared_ptr<RouteHandleIf>)
  > OnCreateCallback;

  /**
   * @param provider that can create single node of RouteHandle tree.
   * @param onCreateRoot callback that will be called every time createRoot
   *        is called. One can modify the root using this callback.
   */
  explicit RouteHandleFactory(RouteHandleProviderIf<RouteHandleIf>& provider,
                              OnCreateCallback onCreateRoot = nullptr);

  /**
   * Creates single RouteHandle from JSON object.
   *
   * @param json object that contains RouteHandle with (optional) children.
   */
  std::shared_ptr<RouteHandleIf> create(const folly::dynamic& json);

  /**
   * Creates root of route handle tree
   *
   * @param json object that contains RouteHandle with (optional) children.
   */
  std::shared_ptr<RouteHandleIf> createRoot(const folly::dynamic& json);

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
  OnCreateCallback onCreateRoot_;

  /// Named routes we've already parsed
  std::unordered_map<std::string,
                     std::vector<std::shared_ptr<RouteHandleIf>>> seen_;

};

}} // facebook::memcache

#include "RouteHandleFactory-inl.h"
