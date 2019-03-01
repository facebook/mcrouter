/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include "WeightedRendezvousHashFunc.h"

#include <cassert>
#include <cmath>

#include "mcrouter/lib/RendezvousHashHelper.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/hash.h"

namespace facebook {
namespace memcache {

WeightedRendezvousHashFunc::WeightedRendezvousHashFunc(
    const std::vector<folly::StringPiece>& endpoints,
    const folly::dynamic& json) {
  checkLogic(
      json.isObject() && json.count("weights"),
      "WeightedRendezvousHashFunc: not an object or no weights");
  checkLogic(
      json["weights"].isObject(),
      "WeightedRendezvousHashFunc: weights is not object");
  const auto& jWeights = json["weights"];

  // Compute hash and decode weight for each endpoint.
  endpointHashes_.reserve(endpoints.size());
  endpointWeights_.reserve(endpoints.size());
  for (const auto ap : endpoints) {
    const uint64_t hash =
        murmur_hash_64A(ap.data(), ap.size(), kRendezvousHashSeed);
    endpointHashes_.push_back(hash);

    if (jWeights.count(ap)) {
      endpointWeights_.push_back(jWeights[ap].asDouble());
    } else {
      LOG(ERROR) << "WeightedRendezvousHashFunc: no weight provided "
                 << "for endpoint " << ap.toString()
                 << ", use default value 0.5";
      endpointWeights_.push_back(0.5);
    }
  }
  assert(endpointHashes_.size() == endpointWeights_.size());
}

size_t WeightedRendezvousHashFunc::operator()(folly::StringPiece key) const {
  double maxScore = 0;
  size_t maxScorePos = 0;

  const uint64_t keyHash =
      murmur_hash_64A(key.data(), key.size(), kRendezvousExtraHashSeed);

  for (size_t i = 0; i < endpointHashes_.size(); ++i) {
    uint64_t scoreInt = hash128to64(endpointHashes_[i], keyHash);
    // Borrow from https://en.wikipedia.org/wiki/Rendezvous_hashing.
    double score = endpointWeights_[i] *
        (1.0 / (-std::log(convertInt64ToDouble01(scoreInt))));
    if (score > maxScore) {
      maxScore = score;
      maxScorePos = i;
    }
  }

  return maxScorePos;
}
} // namespace memcache
} // namespace facebook
