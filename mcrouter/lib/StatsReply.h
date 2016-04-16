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

#include <string>
#include <vector>

#include <folly/Conv.h>

namespace facebook { namespace memcache {

class McReply;
namespace cpp2 {
class McStatsReply;
}
template <class ThriftType>
class TypedThriftReply;

class StatsReply {
 public:
  template <typename V>
  void addStat(folly::StringPiece name, V&& value) {
    stats_.emplace_back(name.str(),
                        folly::to<std::string>(std::forward<V>(value)));
  }

  McReply getMcReply();
  TypedThriftReply<cpp2::McStatsReply> getReply();

 private:
  std::vector<std::pair<std::string, std::string>> stats_;
};

}}  // facebook::memcache
