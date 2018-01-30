/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>
#include <vector>

#include <folly/Conv.h>

namespace facebook {
namespace memcache {

class McStatsReply;

class StatsReply {
 public:
  template <typename V>
  void addStat(folly::StringPiece name, V&& value) {
    stats_.emplace_back(
        name.str(), folly::to<std::string>(std::forward<V>(value)));
  }

  McStatsReply getReply();

 private:
  std::vector<std::pair<std::string, std::string>> stats_;
};
}
} // facebook::memcache
