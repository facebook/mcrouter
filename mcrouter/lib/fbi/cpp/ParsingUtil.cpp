/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ParsingUtil.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {

constexpr uint32_t kMaxTimeout = 1000000;

int64_t parseInt(
    const folly::dynamic& json,
    folly::StringPiece name,
    int64_t min,
    int64_t max) {
  checkLogic(json.isInt(), "{} expected int, found {}", name, json.typeName());
  auto t = json.getInt();
  checkLogic(
      min <= t && t <= max,
      "{} should be in range [{},{}], got {}",
      name,
      min,
      max,
      t);
  return t;
}

bool parseBool(const folly::dynamic& json, folly::StringPiece name) {
  checkLogic(
      json.isBool(), "{} expected bool, found {}", name, json.typeName());
  return json.getBool();
}

std::chrono::milliseconds parseTimeout(
    const folly::dynamic& json,
    folly::StringPiece name) {
  return std::chrono::milliseconds{parseInt(json, name, 1, kMaxTimeout)};
}

folly::StringPiece parseString(
    const folly::dynamic& json,
    folly::StringPiece name) {
  checkLogic(
      json.isString(), "{} expected string, found {}", name, json.typeName());
  return json.stringPiece();
}
}
} // facebook::memcache
