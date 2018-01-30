/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RendezvousHashFunc.h"

#include "mcrouter/lib/fbi/hash.h"

namespace {
constexpr uint64_t kHashSeed = 4193360111ul;
constexpr uint64_t kExtraHashSeed = 2718281828ul;

/**
 * This is the Hash128to64 function from Google's cityhash (available under
 * the MIT License).
 */
inline uint64_t hash128to64(const uint64_t upper, const uint64_t lower) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (lower ^ upper) * kMul;
  a ^= (a >> 47);
  uint64_t b = (upper ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}
} // namespace

namespace facebook {
namespace memcache {

RendezvousHashFunc::RendezvousHashFunc(
    std::vector<folly::StringPiece> endpoints) {
  endpointHashes_.reserve(endpoints.size());
  for (const auto ap : endpoints) {
    const uint64_t hash = murmur_hash_64A(ap.data(), ap.size(), kHashSeed);
    endpointHashes_.push_back(hash);
  }
}

size_t RendezvousHashFunc::operator()(folly::StringPiece key) const {
  uint64_t maxScore = 0;
  size_t maxScorePos = 0;

  const uint64_t keyHash =
      murmur_hash_64A(key.data(), key.size(), kExtraHashSeed);

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
