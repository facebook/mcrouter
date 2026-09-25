/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/carbon/CarbonProtocolReader.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Util.h"
#include "mcrouter/lib/carbon/test/Util.h"

TEST(SerializedFormat, integers) {
  // Serialization for signed integers should output unsigned integers in the
  // following zigzag pattern:
  //  0 -> 0,  -1 -> 1,  1 -> 2,  -2 -> 3,  2 -> 4, etc.
  using VectorPair = std::vector<std::pair<int16_t, int16_t>>;

  auto& matchingRanges = carbon::test::util::satisfiedSubranges<int16_t>;

  EXPECT_EQ(
      (VectorPair{
          {std::numeric_limits<int16_t>::min(),
           std::numeric_limits<int16_t>::max()}}),
      matchingRanges([](int16_t i) {
        const auto zigzagged = carbon::util::zigzag(i);
        if (i >= 0) {
          return 2 * static_cast<uint16_t>(i) == zigzagged;
        } else {
          return 2 * static_cast<uint16_t>(-1 * i) - 1 == zigzagged;
        }
      }));
}

TEST(SerializedFormat, skipContainersWithStopElements) {
  using carbon::FieldType;
  const auto checkSkip = [](FieldType type, std::vector<uint8_t> data) {
    SCOPED_TRACE(static_cast<int>(type));
    constexpr uint8_t kSentinel = 0x42;
    data.push_back(kSentinel);
    auto buffer = folly::IOBuf::copyBuffer(data.data(), data.size());
    carbon::CarbonProtocolReader reader{folly::io::Cursor(buffer.get())};
    reader.skip(type);
    EXPECT_EQ(reader.readRaw<uint8_t>(), kSentinel);
    EXPECT_TRUE(reader.cursor().isAtEnd());
  };

  for (const auto type : {FieldType::List, FieldType::Set}) {
    checkSkip(type, {0xf0, 0xff, 0xff, 0xff, 0xff, 0x0f});
    checkSkip(type, {0x23, 0x11, 0x22});
  }
  for (const uint8_t innerTypes :
       {0x00, 0x01, 0x10, 0x02, 0x20, 0x0e, 0xe0, 0x0f, 0xf0}) {
    SCOPED_TRACE(static_cast<int>(innerTypes));
    checkSkip(FieldType::Map, {0xff, 0xff, 0xff, 0xff, 0x0f, innerTypes});
  }
  checkSkip(FieldType::Map, {0x00});
  checkSkip(FieldType::Map, {0x02, 0x03, 0x11, 0x22});
  checkSkip(FieldType::Map, {0x02, 0x30, 0x11, 0x22});
}
