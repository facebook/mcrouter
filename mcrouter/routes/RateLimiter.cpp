/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RateLimiter.h"

#include <string>

#include <folly/dynamic.h>

#include "mcrouter/lib/fbi/cpp/util.h"

using folly::dynamic;
using std::string;

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

double asPositiveDouble(const dynamic& obj, const string& keyName) {
  checkLogic(obj[keyName].isNumber(), "{} is not a number", keyName);
  auto d = obj[keyName].asDouble();
  checkLogic(d > 0.0, "{} is nonpositive", keyName);
  return d;
}

double asPositiveDoubleDefault(const dynamic& obj, const string& keyName,
                               double def) {
  if (obj.count(keyName) && obj[keyName].isNumber()) {
    auto d = obj[keyName].asDouble();
    checkLogic(d > 0.0, "{} is nonpositive", keyName);
    return d;
  }
  return def;
}

}  // namespace

RateLimiter::RateLimiter(const folly::dynamic& json) {
  checkLogic(json.isObject(), "RateLimiter settings json is not an object");

  auto now = TokenBucket::defaultClockNow();

  if (json.count("gets_rate")) {
    double rate = asPositiveDouble(json, "gets_rate");
    double burst = asPositiveDoubleDefault(json, "gets_burst", rate);
    getsTb_ = TokenBucket(rate, burst, now);
  }

  if (json.count("sets_rate")) {
    double rate = asPositiveDouble(json, "sets_rate");
    double burst = asPositiveDoubleDefault(json, "sets_burst", rate);
    setsTb_ = TokenBucket(rate, burst, now);
  }

  if (json.count("deletes_rate")) {
    double rate = asPositiveDouble(json, "deletes_rate");
    double burst = asPositiveDoubleDefault(json, "deletes_burst", rate);
    deletesTb_ = TokenBucket(rate, burst, now);
  }
}

}}}  // facebook::memcache::mcrouter
