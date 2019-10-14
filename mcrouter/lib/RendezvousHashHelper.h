/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <inttypes.h>

namespace facebook {
namespace memcache {

constexpr uint64_t kRendezvousHashSeed = 4193360111ul;
constexpr uint64_t kRendezvousExtraHashSeed = 2718281828ul;

/**
 * This is the Hash128to64 function from Google's cityhash (available under
 * the MIT License).
 */
inline uint64_t hash128to64(const uint64_t upper, const uint64_t lower) {
  // Murmur-inspired hashing.
  constexpr uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (lower ^ upper) * kMul;
  a ^= (a >> 47);
  uint64_t b = (upper ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

/**
 * Converts a uniformly random 64-bit integer to uniformly random
 * floating point number on interval [0, 1)
 */
inline double convertInt64ToDouble01(const uint64_t value) {
  constexpr uint64_t fiftyThreeOnes = (0xFFFFFFFFFFFFFFFF >> (64 - 53));
  constexpr double fiftyThreeZeros = ((uint64_t)1) << 53;
  return (value & fiftyThreeOnes) / fiftyThreeZeros;
}

} // namespace memcache
} // namespace facebook
