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

#include <string>

namespace facebook { namespace memcache { namespace mcrouter {

enum class RequestClass : uint8_t {
  NORMAL,
  FAILOVER,
  SHADOW,
};

const char* const requestClassStr(RequestClass requestClass);

struct McrouterStackContext {
  const std::string* asynclogName{nullptr};
  RequestClass requestClass{RequestClass::NORMAL};
  bool failoverTag{false};
};

}}}  // facebook::memcache::mcrouter
