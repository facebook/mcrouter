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
#include <map>

namespace facebook { namespace memcache { namespace cycles {

namespace detail {

/**
 * An instance of GkTuple is stored together with each value
 * inserted in QuantilesCalculator.
 */
struct GkTuple {
  // The sum (g0 + g1 + ... + gi) gives the minimum rank (starting at 1) that
  // element i represents (this sum is called rMin).
  uint64_t g;

  // (rMin + delta) gives the max rank that an element represents
  // (this is called rMax).
  uint64_t delta;
};

} // detail

/**
 * Space and time efficient quantile calculator for data streams.
 * This is an implementation of Greenwald-Khanna (GK) algorithm
 * (paper: http://dl.acm.org/citation.cfm?id=375670).
 */
template<class T>
class QuantilesCalculator {
 public:
  static_assert(std::is_arithmetic<T>::value, "T must be a numeric type.");

  /**
   * Creates a new instance of quantiles calculator.
   *
   * @param eps   Margin of error allowed. The smaller the margin is, the
   *              larger is the space used by the data structure.
   *              The maximum amount of memory used by this data structure, can
   *              be roughly calculated by the formula:
   *              "(1 / eps) * log2(n * eps)",
   *              where "eps" is the margin of error and "n" is number of
   *              elements inserted into the data structure.
   *              Note: eps > 0.0
   */
  explicit QuantilesCalculator(double eps = 0.001);

  /**
   * Adds a sample
   *
   * @param v   Sample value.
   */
  void insert(T v);

  /**
   * Returns the value of the "q" quantile, with maximum error of "eps" (i.e.
   * return the value of a quantile in [q-eps, q+eps] range).
   *
   * @param q   Quantile (0 <= q <= 1) desired.
   */
  T query(double q) const;

  /**
   * Returns the number of samples inserted so far.
   */
  uint64_t size() const {
    return n_;
  }

  /**
   * Returns the size being actually used by the data structure.
   * Useful for debugging.
   */
  uint64_t internalSize() const {
    return summary_.size();
  }

 private:
  std::multimap<T, detail::GkTuple> summary_;
  uint64_t n_{0};
  uint64_t inserts_{0};
  const double eps_;

  void compress();
};

}}} // namespace facebook::memcache::cycles

#include "QuantilesCalculator-inl.h"
