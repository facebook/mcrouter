/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <memory>
#include <vector>

#include <folly/Range.h>
#include <folly/dynamic.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleProviderIf;

/**
 * Parses RouteHandle tree from JSON object.
 */
template <class RouteHandleIf>
class RouteHandleFactory {
 public:
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;

  RouteHandleFactory(const RouteHandleFactory&) = delete;
  RouteHandleFactory& operator=(const RouteHandleFactory&) = delete;

  /**
   * @param provider  creates single node of RouteHandle tree
   * @param threadId  thread where route handles will run
   */
  RouteHandleFactory(
      RouteHandleProviderIf<RouteHandleIf>& provider,
      size_t threadId);

  /**
   * Adds a named route handle that may be used later.
   *
   * @param json object that contains RouteHandle with (optional) children.
   */
  void addNamed(folly::StringPiece name, folly::dynamic json);

  /**
   * Creates single RouteHandle from JSON object.
   *
   * @param json object that contains RouteHandle with (optional) children.
   */
  RouteHandlePtr create(const folly::dynamic& json);

  /**
   * Creates multiple subtrees from JSON object. Should be used to create
   * children of some RouteHandle.
   *
   * @param json array, object or string that represents zero, one or multiple
   *             RouteHandles.
   */
  std::vector<RouteHandlePtr> createList(const folly::dynamic& json);

  /**
   * Loads a pool from ConfigApi, expand `inherit`, etc.
   *
   * @param json  Json with the pool information.
   *
   * @return      The folly::dynamic object with pool name and final json blob.
   */
  const folly::dynamic& parsePool(const folly::dynamic& json);

  size_t getThreadId() const noexcept {
    return threadId_;
  }

 private:
  RouteHandleProviderIf<RouteHandleIf>& provider_;

  /// Registered named routes that are not parsed yet
  folly::StringKeyedUnorderedMap<folly::dynamic> registered_;
  /// Named routes we've already parsed
  folly::StringKeyedUnorderedMap<std::vector<RouteHandlePtr>> seen_;
  /// Thread where route handles created by this factory will be used
  size_t threadId_;

  const std::vector<RouteHandlePtr>& createNamed(
      folly::StringPiece name,
      const folly::dynamic& json);
};

} // namespace memcache
} // namespace facebook

#include "RouteHandleFactory-inl.h"
