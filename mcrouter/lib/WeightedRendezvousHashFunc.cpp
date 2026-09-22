/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "WeightedRendezvousHashFunc.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>

#include "mcrouter/lib/RendezvousHashHelper.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/hash.h"

namespace facebook {
namespace memcache {

namespace {

/* run once in the constructor to see if it is possible to set
 * uniformPositiveWeights_, which means the pool is eligble to skip std::log
 * since weights are uniform
 */
bool allWeightsEqualAndPositive(const std::vector<double>& weights) {
  return !weights.empty() && weights.front() > 0 &&
      std::adjacent_find(
          weights.begin(), weights.end(), std::not_equal_to<>()) ==
      weights.end();
}

// For a single positive weight w shared by every candidate, the score
// w * 1/(-log(U)) is strictly increasing in U. Argmax of the
// score is argmax of the masked hash, so the log, the divide and the multiply
// are all dead work.
size_t selectByMaskedHash(
    const std::vector<uint64_t>& endpointHashes,
    uint64_t keyHash) {
  uint64_t maxMaskedHash = 0;
  size_t maxPos = 0;
  for (size_t i = 0; i < endpointHashes.size(); ++i) {
    const uint64_t maskedHash =
        hash128to64(endpointHashes[i], keyHash) & kFiftyThreeOnes;
    if (maskedHash > maxMaskedHash) {
      maxMaskedHash = maskedHash;
      maxPos = i;
    }
  }
  return maxPos;
}
} // namespace

WeightedRendezvousHashFunc::WeightedRendezvousHashFunc(
    const std::vector<folly::StringPiece>& endpoints,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "WeightedRendezvousHashFunc: not an object");
  checkLogic(json.count("weights"), "WeightedRendezvousHashFunc: no weights");
  const auto& jWeights = json["weights"];
  checkLogic(
      jWeights.isArray(),
      "WeightedRendezvousHashFunc: weights is not an array");

  checkLogic(
      jWeights.size() == endpoints.size(),
      "WeightedRendezvousHash: number of weights doesn't match number of end points.");

  // Compute hash and decode weight for each endpoint.
  endpointHashes_.reserve(endpoints.size());
  endpointWeights_.reserve(endpoints.size());
  assert(endpointHashes_.empty());
  assert(endpointWeights_.empty());

  for (size_t i = 0; i < endpoints.size(); ++i) {
    const uint64_t hash = murmur_hash_64A(
        endpoints[i].data(), endpoints[i].size(), kRendezvousHashSeed);
    endpointHashes_.push_back(hash);
    endpointWeights_.push_back(jWeights[i].asDouble());
  }

  uniformPositiveWeights_ = allWeightsEqualAndPositive(endpointWeights_);
}

size_t WeightedRendezvousHashFunc::operator()(folly::StringPiece key) const {
  const uint64_t keyHash =
      murmur_hash_64A(key.data(), key.size(), kRendezvousExtraHashSeed);

  if (uniformPositiveWeights_) {
    return selectByMaskedHash(endpointHashes_, keyHash);
  }

  double maxScore = 0;
  size_t maxScorePos = 0;

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

namespace {

// Same argmax-preserving substitution as selectByMaskedHash()
std::vector<RendezvousIterator::ScoreAndIndex> rankByMaskedHash(
    const std::vector<uint64_t>& endpointHashes,
    uint64_t keyHash) {
  std::vector<RendezvousIterator::ScoreAndIndex> scores;
  scores.reserve(endpointHashes.size());
  for (size_t pos = 0; pos < endpointHashes.size(); ++pos) {
    const uint64_t maskedHash =
        hash128to64(endpointHashes[pos], keyHash) & kFiftyThreeOnes;
    scores.emplace_back(static_cast<double>(maskedHash), pos);
  }
  return scores;
}

std::vector<RendezvousIterator::ScoreAndIndex> get_scores(
    const std::vector<uint64_t>& endpointHashes,
    const std::vector<double>& endpointWeights,
    const bool uniformPositiveWeights,
    const folly::StringPiece key) {
  const uint64_t keyHash = RendezvousIterator::keyHash(key);

  if (uniformPositiveWeights) {
    return rankByMaskedHash(endpointHashes, keyHash);
  }

  std::vector<RendezvousIterator::ScoreAndIndex> scores;
  scores.reserve(endpointHashes.size());

  for (size_t pos = 0; pos < endpointHashes.size(); ++pos) {
    const uint64_t scoreInt = hash128to64(endpointHashes[pos], keyHash);
    const double scoreDouble = convertInt64ToDouble01(scoreInt);

    // Borrow from https://en.wikipedia.org/wiki/Rendezvous_hashing.
    double score = endpointWeights[pos] * (1.0 / (-std::log(scoreDouble)));

    scores.emplace_back(score, pos);
  }

  return scores;
}
} // namespace

WeightedRendezvousHashFunc::Iterator::Iterator(
    const std::vector<uint64_t>& hashes,
    const std::vector<double>& endpointWeights,
    const bool uniformPositiveWeights,
    const folly::StringPiece key)
    : RendezvousIterator(
          get_scores(hashes, endpointWeights, uniformPositiveWeights, key)) {}

} // namespace memcache
} // namespace facebook
