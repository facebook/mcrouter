/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/Range.h>
#include <folly/dynamic.h>

namespace facebook {
namespace memcache {

/**
 * Weighted Rendezvous hashing based on RendezvousHashFunc.
 * Each server is assigned a weight between 0.0 and 1.0 inclusive.
 */
class WeightedRendezvousHashFunc {
 public:
  /**
   * @param endpoints   A list of backend servers.
   * @param json        The weights for backend servers. Format:
   *                    {
   *                      "weights" : {
   *                         "server1": <weight1>,
   *                         "server2": <weight2>,
   *                         ...
   *                      },
   *                      ...
   *                    }
   */
  WeightedRendezvousHashFunc(
      const std::vector<folly::StringPiece>& endpoints,
      const folly::dynamic& json);

  size_t operator()(folly::StringPiece key) const;

  static const char* type() {
    return "WeightedRendezvous";
  }

 private:
  std::vector<uint64_t> endpointHashes_;
  std::vector<double> endpointWeights_;
};
} // namespace memcache
} // namespace facebook
