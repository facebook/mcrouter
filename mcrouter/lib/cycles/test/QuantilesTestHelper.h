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

#include <cstdint>
#include <limits>
#include <vector>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/stats.hpp>

#include <folly/Random.h>

namespace facebook { namespace memcache { namespace cycles { namespace test {

class ExactCalculator {
 public:
  void insert(uint64_t elem) {
    all_.push_back(elem);
  }
  uint64_t query(double q) {
    std::sort(all_.begin(), all_.end());
    return all_[(all_.size() - 1) * q];
  }
 private:
  std::vector<uint64_t> all_;
};

typedef boost::accumulators::accumulator_set<
  uint64_t, boost::accumulators::stats<
    boost::accumulators::tag::p_square_quantile
  >> accumulator_t;
class BoostCalculator {
 public:
  void insert(uint64_t v) {
    acc_(v);
  }
  uint64_t query() {
    return boost::accumulators::p_square_quantile(acc_);
  }
 private:
  accumulator_t acc_{boost::accumulators::quantile_probability = 0.5};
};

uint64_t normalRnd() {
  static std::mt19937 gen(folly::randomNumberSeed());
  static std::normal_distribution<> d(10000,50);

  return d(gen);
}

}}}} // facebook::memcache::cycles::test
