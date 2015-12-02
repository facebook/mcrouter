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
#include <vector>

#include <folly/dynamic.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/Range.h>

namespace facebook { namespace memcache {

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
   * @param provider that can create single node of RouteHandle tree.
   */
  explicit RouteHandleFactory(RouteHandleProviderIf<RouteHandleIf>& provider);

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
 private:
  RouteHandleProviderIf<RouteHandleIf>& provider_;

  /// Registered named routes that are not parsed yet
  folly::StringKeyedUnorderedMap<folly::dynamic> registered_;
   /// Named routes we've already parsed
  folly::StringKeyedUnorderedMap<std::vector<RouteHandlePtr>> seen_;

  const std::vector<RouteHandlePtr>&
  createNamed(folly::StringPiece name, const folly::dynamic& json);
};

}} // facebook::memcache

#include "RouteHandleFactory-inl.h"
