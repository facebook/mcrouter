/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "RendezvousHashFunc.h"

#include "mcrouter/lib/RendezvousHashHelper.h"
#include "mcrouter/lib/fbi/hash.h"

namespace facebook {
namespace memcache {

RendezvousHashFunc::RendezvousHashFunc(
    const std::vector<folly::StringPiece>& endpoints) {
  endpointHashes_.reserve(endpoints.size());
  for (const auto ap : endpoints) {
    const uint64_t hash =
        murmur_hash_64A(ap.data(), ap.size(), kRendezvousHashSeed);
    endpointHashes_.push_back(hash);
  }
}

size_t RendezvousHashFunc::operator()(folly::StringPiece key) const {
  uint64_t maxScore = 0;
  size_t maxScorePos = 0;

  const uint64_t keyHash =
      murmur_hash_64A(key.data(), key.size(), kRendezvousExtraHashSeed);

  size_t pos = 0;
  for (const auto hash : endpointHashes_) {
    const uint64_t score = hash128to64(hash, keyHash);
    if (score > maxScore) {
      maxScore = score;
      maxScorePos = pos;
    }
    ++pos;
  }

  return maxScorePos;
}
} // namespace memcache
} // namespace facebook
