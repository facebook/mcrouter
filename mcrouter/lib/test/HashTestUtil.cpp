/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "HashTestUtil.h"

namespace facebook {
namespace memcache {
namespace test {

std::pair<std::vector<std::string>, std::vector<folly::StringPiece>>
genEndpoints(int n) {
  std::vector<std::string> raw;
  std::vector<folly::StringPiece> ref;
  for (int i = 0; i < n; ++i) {
    auto endpoint = "xxx." + folly::to<std::string>(i) + ".yy";
    raw.push_back(endpoint);
  }
  for (const auto& e : raw) {
    ref.push_back(e);
  }
  return std::make_pair(std::move(raw), std::move(ref));
}

folly::dynamic genWeights(
    const std::vector<folly::StringPiece>& endpoints,
    const std::vector<double>& weights) {
  folly::dynamic jWeights = folly::dynamic::object;

  for (size_t i = 0; i < endpoints.size(); i++) {
    jWeights[endpoints[i]] = weights[i];
  }

  folly::dynamic json = folly::dynamic::object;
  json["weights"] = jWeights;
  return json;
}

} // namespace test
} // namespace memcache
} // namespace facebook
