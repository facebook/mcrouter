/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <limits>
#include <string>

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

} // util
} // test
} // carbon

#include "Util-inl.h"
