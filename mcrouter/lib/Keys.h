/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Range.h>

namespace facebook { namespace memcache {

/**
 * Holds all the references to the various parts of the key.
 *
 *                        /region/cluster/foo:key|#|etc
 * keyData_:              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * keyWithoutRoute:                       ^^^^^^^^^^^^^
 * routingPrefix:         ^^^^^^^^^^^^^^^^
 * routingKey:                            ^^^^^^^
 */
class Keys {
 public:
  constexpr Keys() = default;
  explicit Keys(folly::StringPiece key) noexcept;
  Keys(const Keys& other) = default;
  Keys& operator=(const Keys& other) = default;

  folly::StringPiece keyWithoutRoute() const {
    return keyWithoutRoute_;
  }

  folly::StringPiece routingPrefix() const {
    return routingPrefix_;
  }

  folly::StringPiece routingKey() const {
    return routingKey_;
  }

  void update(folly::StringPiece key);
  uint32_t routingKeyHash() const;

  // Hack to save some CPU in McRequest::stripRoutingPrefix. Avoid if possible.
  void clearRoutingPrefix() {
    routingPrefix_.clear();
  }
 private:
  folly::StringPiece keyWithoutRoute_;
  folly::StringPiece routingPrefix_;
  folly::StringPiece routingKey_;
  uint32_t routingKeyHash_{0};
};

}} // facebook::memcache
