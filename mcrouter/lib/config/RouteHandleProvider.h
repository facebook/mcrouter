/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleProviderIf.h"

namespace facebook { namespace memcache {

/**
 * Implementation of RouteHandleProviderIf that can create some basic routes
 * like AllInitial, AllSync, Hash, etc.
 */
template <class RouteHandleIf>
class RouteHandleProvider : public RouteHandleProviderIf<RouteHandleIf> {
 public:
  virtual std::vector<std::shared_ptr<RouteHandleIf>>
  create(RouteHandleFactory<RouteHandleIf>& factory, const std::string& type,
         const folly::dynamic& json);

  virtual ~RouteHandleProvider() {};
 protected:

  /**
   * Creates HashRoute by given HashFunc type.
   *
   * @param factory RouteHandleFactory to create children routes.
   * @param funcType type of HashFunc used for this HashRoute.
   * @param json object with HashRoute representation.
   * @param children route handles HashRoute will route to.
   */
  virtual std::shared_ptr<RouteHandleIf>
  createHash(const std::string& funcType,
             const folly::dynamic& json,
             std::vector<std::shared_ptr<RouteHandleIf>> children);

  /**
   * Helper method to parse "hash_func" and create corresponding HashRoute.
   */
  std::shared_ptr<RouteHandleIf>
  makeHash(const folly::dynamic& json,
           std::vector<std::shared_ptr<RouteHandleIf>> children);
};

}} // facebook::memcache

#include "RouteHandleProvider-inl.h"
