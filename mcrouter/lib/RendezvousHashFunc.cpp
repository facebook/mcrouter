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
  Iterator iter{endpointHashes_, key};
  return *iter;
}

RendezvousHashFunc::Iterator::Iterator(
    const std::vector<uint64_t>& hashes,
    folly::StringPiece key)
    : queue_(make_queue(hashes, key)) {}

RendezvousHashFunc::Iterator& RendezvousHashFunc::Iterator::operator++() {
  if (!queue_.empty()) {
    queue_.pop();
  }

  return *this;
}

std::priority_queue<RendezvousHashFunc::Iterator::ScoreAndIndex>
RendezvousHashFunc::Iterator::make_queue(
    const std::vector<uint64_t>& endpointHashes,
    const folly::StringPiece& key) {
  std::vector<ScoreAndIndex> scores;

  const uint64_t keyHash =
      murmur_hash_64A(key.data(), key.size(), kRendezvousExtraHashSeed);

  scores.reserve(endpointHashes.size());
  for (size_t pos = 0; pos < endpointHashes.size(); ++pos) {
    const uint64_t score = hash128to64(endpointHashes[pos], keyHash);
    scores.emplace_back(ScoreAndIndex{score, pos});
  }

  return std::priority_queue<ScoreAndIndex>(
      std::less<ScoreAndIndex>(), std::move(scores));
}

} // namespace memcache
} // namespace facebook
