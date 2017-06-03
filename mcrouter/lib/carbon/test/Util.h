/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/Range.h>

namespace carbon {
namespace test {

class SimpleStruct;
class TestRequest;

namespace util {

constexpr auto kMinInt8 = std::numeric_limits<int8_t>::min();
constexpr auto kMinInt16 = std::numeric_limits<int16_t>::min();
constexpr auto kMinInt32 = std::numeric_limits<int32_t>::min();
constexpr auto kMinInt64 = std::numeric_limits<int64_t>::min();

constexpr auto kMaxUInt8 = std::numeric_limits<uint8_t>::max();
constexpr auto kMaxUInt16 = std::numeric_limits<uint16_t>::max();
constexpr auto kMaxUInt32 = std::numeric_limits<uint32_t>::max();
constexpr auto kMaxUInt64 = std::numeric_limits<uint64_t>::max();

constexpr folly::StringPiece kShortString = "aaaaaaaaaa";

std::string longString();
void expectEqSimpleStruct(const SimpleStruct& a, const SimpleStruct& b);
void expectEqTestRequest(const TestRequest& a, const TestRequest& b);

template <class T>
T serializeAndDeserialize(const T&);

template <class T>
T serializeAndDeserialize(const T&, size_t& bytesWritten);

/**
 * Takes a boolean predicate and returns the list of subranges over
 * [numeric_limits<T>::min(), numeric_limits<T>::max()] satisfying the
 * predicate.
 */
template <class T>
std::vector<std::pair<T, T>> satisfiedSubranges(std::function<bool(T)> pred) {
  static_assert(
      std::is_integral<T>::value,
      "satisfiedSubranges may only be used over ranges of integers");

  bool inSatisfiedRange = false;
  T startRange;
  T endRange;
  std::vector<std::pair<T, T>> satisfiedRanges;

  auto i = std::numeric_limits<T>::min();
  while (true) {
    const bool satisfied = pred(i);
    if (satisfied && !inSatisfiedRange) {
      inSatisfiedRange = true;
      startRange = i;
    } else if (!satisfied && inSatisfiedRange) {
      inSatisfiedRange = false;
      endRange = i - 1;
      satisfiedRanges.emplace_back(startRange, endRange);
    }

    if (i == std::numeric_limits<T>::max()) {
      break;
    }
    ++i;
  }

  if (inSatisfiedRange) {
    endRange = i;
    satisfiedRanges.emplace_back(startRange, endRange);
  }

  return satisfiedRanges;
}

} // util
} // test
} // carbon

#include "Util-inl.h"
