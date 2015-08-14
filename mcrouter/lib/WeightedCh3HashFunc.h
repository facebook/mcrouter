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

#include <unordered_map>

#include <folly/Range.h>

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

/**
 * A weighted CH3 hash function.
 *
 * Each server is assigned a weight between 0.0 and 1.0 inclusive.
 * The algorithm:
 *
 *   Try 32 times:
 *     index = CH3(key + next_salt(), n)
 *     probability = SpookyHashV2_uint32(key)
 *     if (probability < server_weight[index] * uint32_max):
 *       return index
 *   return index
 *
 * Where next_salt() initially returns an empty string, and subsequently
 * distinct salt strings.
 * (the actual salts used are strings "0", "1", ..., "9", "01", "11", "21",
 *  i.e. reversed decimal representations of an increasing counter).
 *
 * Note that if all weights are 1.0, the algorithm returns the same indices
 * as simply CH3(key, n).
 *
 * The algorithm is consistent both with respect to n and to individual weights,
 * i.e. reducing any single weight slightly will only spread out
 * a small fraction of the load from that server to all other servers.
 */
class WeightedCh3HashFunc {
 public:
  /**
   * @param weights  A list of server weights.
   *                 Pool size is taken to be weights.size()
   */
  explicit WeightedCh3HashFunc(std::vector<double> weights);

  WeightedCh3HashFunc(const folly::dynamic& json, size_t n);

  size_t operator()(folly::StringPiece key) const;

  /**
   * @return Saved weights.
   */
  const std::vector<double>& weights() const {
    return weights_;
  }

  static const char* type() {
    return "WeightedCh3";
  }

 private:
  std::vector<double> weights_;
};

}}  // facebook::memcache
