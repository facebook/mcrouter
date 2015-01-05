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
#include <vector>

#include <folly/Range.h>

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

/**
 * Interface for RouteHandleProvider to easily mock it. Implementation
 * should create corresponding RouteHandle by given type and JSON.
 */
template <class RouteHandleIf>
class RouteHandleProviderIf {
 public:
  /**
   * Creates list of RouteHandles by given type and JSON representation. Should
   * also validate passed object and throw exception in case JSON is incorrect.
   *
   * @param factory RouteHandleFactory to create children routes.
   * @param type which RouteHandle is represented by json.
   * @param json JSON object with RouteHandle representation.
   */
  virtual std::vector<std::shared_ptr<RouteHandleIf>>
  create(RouteHandleFactory<RouteHandleIf>& factory, folly::StringPiece type,
         const folly::dynamic& json) = 0;

  virtual ~RouteHandleProviderIf() {};
};

}} // facebook::memcache
