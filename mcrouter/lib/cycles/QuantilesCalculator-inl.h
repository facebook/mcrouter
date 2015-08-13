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

#include <limits>

namespace facebook { namespace memcache { namespace cycles {

template<class T>
QuantilesCalculator<T>::QuantilesCalculator(double eps)
    : eps_(eps) {
  assert(eps > 0.0);
  summary_.emplace(std::numeric_limits<T>::max(), detail::GkTuple{1, 0});
}

template<class T>
void QuantilesCalculator<T>::insert(T v) {
  ++n_;

  const uint64_t epsN = 2 * eps_ * n_;

  // We need to insert this cell after the cells that share the same value.
  // Getting the cell just after the new one to use as a hint for the insert.
  auto ub = summary_.upper_bound(v);

  // if the next cell (upper bound) is not full, just increment it's g.
  // (This is an optimization - it's not described in the paper.
  // In the compress phase it would happen anyway).
  if ((ub->second.g + ub->second.delta) < epsN) {
    ub->second.g++;
    return;
  }

  // We keep track of number of inserts to know when it's time to compress().
  ++inserts_;

  // Using upper_bound as a hint to where to insert it.
  auto cur = summary_.insert(ub, std::make_pair(v, detail::GkTuple{1, epsN}));
  if (cur == summary_.begin()) {
    cur->second.delta = 0;
  }

  // We need to compress once every 1/(2*eps_) to keep our memory usage goals.
  assert(uint64_t(1 / (2 * eps_)) != 0);
  if ((inserts_ % static_cast<uint64_t>(1 / (2 * eps_))) == 0) {
    compress();
  }
}

template<class T>
T QuantilesCalculator<T>::query(double q) const {
  assert(q >= 0.0 && q <= 1.0);

  // r represents the rank of the element we want (i.e. if we were
  // storing the entire data stream in a sorted array, the answer would be
  // as simple as: return array[r]).
  const uint64_t r = n_ * q + 0.5;

  // epsN represents the error margin in rank (i.e. if we were storing the
  // entire data stream in a sorted array and the right answer was array[r],
  // anything in the range "array[r +- epsN]" would be a valid answer).
  const uint64_t epsN = eps_ * n_;

  // rMin represents the minimum rank represented by current element.
  // rMin + delta = rMax, which is the maximum rank represented by
  // current element.
  uint64_t rMin = 0;

  // We find a solution as soon as we find an element where
  // rMin <= r <= rMax.
  auto cur = summary_.begin();
  auto next = cur;
  while (++next != summary_.end()) {
    rMin += cur->second.g;
    if ((r + epsN) < (rMin + next->second.g)) {
      break;
    }
    cur = next;
  }

  return cur->first;
}

/**
 * We might have overlapping rMin/rMax pairs. This method simply
 * gets rid of these useless elements.
 */
template<class T>
void QuantilesCalculator<T>::compress() {
  uint64_t epsN = 2 * eps_ * n_;

  auto cur = summary_.begin();
  auto next = cur;
  while (++next != summary_.end()) {
    if ((cur->second.g + next->second.g + next->second.delta) < epsN) {
      next->second.g += cur->second.g;
      summary_.erase(cur);
    }
    cur = next;
  }
}

}}} // facebook::memcache::cycles
