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
#include <folly/Range.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Hashes routing_key using provided function and routes to the destination
 */
template <class RouteHandleIf, typename HashFunc>
class HashRoute {
 public:
  static std::string routeName() { return "hash:" + HashFunc::type(); }

  HashRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh,
            std::string salt,
            HashFunc hashFunc)
    : rh_(std::move(rh)),
      salt_(std::move(salt)),
      hashFunc_(std::move(hashFunc)) {
  }

  HashRoute(const folly::dynamic& json,
            std::vector<std::shared_ptr<RouteHandleIf>> children)
    : rh_(std::move(children)),
      hashFunc_(json, rh_.size()) {

    if (json.isObject() && json.count("salt")) {
      checkLogic(json["salt"].isString(), "HashRoute salt is not a string");
      salt_ = json["salt"].getString().toStdString();
    }
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return {rh_[pick(req)]};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    if (rh_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    } else {
      return rh_[pickInMainContext(req)]->route(req, Operation());
    }
  }

 private:
  static const size_t kMaxKeySaltSize = 512;
  const std::vector<std::shared_ptr<RouteHandleIf>> rh_;
  std::string salt_;
  HashFunc hashFunc_;

  template <class Request>
  size_t pick(const Request& req) const {
    size_t n = 0;
    if (salt_.empty()) {
      n = hashFunc_(req.routingKey());
    } else {
      // fast string concatenation
      char c[kMaxKeySaltSize];
      auto key = req.routingKey();
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
    return fiber::runInMainContext([this, &req] () {
        /* this-> here is necessary for gcc-4.7 - it can't find pick()
           without it */
        return this->pick(req);
      }
    );
  }
};

}}
