/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <functional>
#include <stdexcept>
#include <string>

#include <mcrouter/lib/Operation.h>

namespace carbon {

class CarbonConnectionRecreateException : public std::runtime_error {
 public:
  explicit CarbonConnectionRecreateException(const std::string& what)
      : std::runtime_error(what) {}
};

class CarbonConnectionException : public std::runtime_error {
 public:
  explicit CarbonConnectionException(const std::string& what)
      : std::runtime_error(what) {}
};

template <class Request>
using RequestCb =
    std::function<void(const Request&, facebook::memcache::ReplyT<Request>&&)>;
} // carbon
