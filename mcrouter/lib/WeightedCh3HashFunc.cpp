/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "WeightedCh3HashFunc.h"

#include <folly/dynamic.h>
#include <folly/SpookyHashV2.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/hash.h"

namespace facebook { namespace memcache {

namespace {
const size_t kNumTries = 32;
const uint32_t kHashSeed = 0xface2014;
}  // anonymous namespace

WeightedCh3HashFunc::WeightedCh3HashFunc(
  std::vector<double> weights)
    : weights_(std::move(weights)) {
}

WeightedCh3HashFunc::WeightedCh3HashFunc(const folly::dynamic& json, size_t n) {
  checkLogic(json.isObject() && json.count("weights"),
             "WeightedCh3HashFunc: not an object or no weights");
  checkLogic(json["weights"].isArray(),
             "WeightedCh3HashFunc: weights is not array");
  const auto& jWeights = json["weights"];
  LOG_IF(ERROR, jWeights.size() < n)
    << "WeightedCh3HashFunc: CONFIG IS BROKEN!!! number of weights ("
    << jWeights.size() << ") is smaller than number of servers (" << n
    << "). Missing weights are set to 0.5";
  for (size_t i = 0; i < std::min(n, jWeights.size()); ++i) {
    const auto& weight = jWeights[i];
    checkLogic(weight.isNumber(), "WeightedCh3HashFunc: weight is not number");
    weights_.push_back(weight.asDouble());
  }
  weights_.resize(n, 0.5);
}

size_t WeightedCh3HashFunc::operator()(folly::StringPiece key) const {
  auto n = weights_.size();
  checkLogic(n && n <= furc_maximum_pool_size(), "Invalid pool size: {}", n);
  size_t salt = 0;
  size_t index = 0;
  std::string saltedKey;
  auto originalKey = key;
  for (size_t i = 0; i < kNumTries; ++i) {
    index = furc_hash(key.data(), key.size(), n);

    /* Use 32-bit hash, but store in 64-bit ints so that
       we don't have to deal with overflows */
    uint64_t p = folly::hash::SpookyHashV2::Hash32(key.data(), key.size(),
                                                   kHashSeed);
    assert(0 <= weights_[index] && weights_[index] <= 1.0);
    uint64_t w = weights_[index] * std::numeric_limits<uint32_t>::max();

    /* Rehash only if p is out of range */
    if (LIKELY(p < w)) {
      return index;
    }

    /* Change the key to rehash */
    auto s = salt++;
    saltedKey = originalKey.str();
    do {
      saltedKey.push_back(char(s % 10) + '0');
      s /= 10;
    } while (s > 0);

    key = saltedKey;
  }

  return index;
}

}}  // facebook::memcache
