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
#include <vector>

#include <folly/Conv.h>

namespace facebook { namespace memcache {

class McReply;

class StatsReply {
 public:
  template <typename V>
  void addStat(folly::StringPiece name, V value) {
    stats_.push_back(make_pair(name.str(), folly::to<std::string>(value)));
  }

  McReply getMcReply();

 private:
  std::vector<std::pair<std::string, std::string>> stats_;
};

}}  // facebook::memcache
