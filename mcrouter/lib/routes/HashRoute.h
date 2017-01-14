/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/fibers/FiberManager.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {

/**
 * Hashes routing_key using provided function and routes to the destination
 */
template <class RouteHandleIf, typename HashFunc>
class HashRoute {
 public:
  std::string routeName() const {
    return folly::to<std::string>(
        "hash|", HashFunc::type(), (salt_.empty() ? "" : "|salt=" + salt_));
  }

  HashRoute(
      std::vector<std::shared_ptr<RouteHandleIf>> rh,
      std::string salt,
      HashFunc hashFunc)
      : rh_(std::move(rh)),
        salt_(std::move(salt)),
        hashFunc_(std::move(hashFunc)) {
    assert(!rh_.empty());
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*rh_[pickInMainContext(req)], req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return rh_[pickInMainContext(req)]->route(req);
  }

 private:
  static const size_t kMaxKeySaltSize = 512;
  const std::vector<std::shared_ptr<RouteHandleIf>> rh_;
  const std::string salt_;
  const HashFunc hashFunc_;

  template <class Request>
  size_t pick(const Request& req) const {
    size_t n = 0;
    if (salt_.empty()) {
      n = hashFunc_(req.key().routingKey());
    } else {
      // fast string concatenation
      char c[kMaxKeySaltSize];
      auto key = req.key().routingKey();
      auto keySaltSize = key.size() + salt_.size();
      if (UNLIKELY(keySaltSize >= kMaxKeySaltSize)) {
        throw std::runtime_error("Salted key too long: " + key.str() + salt_);
      }
      memcpy(c, key.data(), key.size());
      memcpy(c + key.size(), salt_.data(), salt_.size());

      n = hashFunc_(folly::StringPiece(c, c + keySaltSize));
    }
    if (UNLIKELY(n >= rh_.size())) {
      throw std::runtime_error("index out of range");
    }
    return n;
  }

  template <class Request>
  size_t pickInMainContext(const Request& req) const {
    /* Hash functions can be stack-intensive,
       so jump back to the main context */
    return folly::fibers::runInMainContext([this, &req]() {
      /* this-> here is necessary for gcc-4.7 - it can't find pick()
         without it */
      return this->pick(req);
    });
  }
};
}
} // facebook::memcache
