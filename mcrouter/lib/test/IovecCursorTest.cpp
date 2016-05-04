/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/IovecCursor.h"

using namespace facebook::memcache;

namespace {

IovecCursor getIovecCursor(const std::vector<std::string>& buffers) {
  constexpr size_t kMaxIovLen = 16;
  EXPECT_LT(buffers.size(), 16);

  static struct iovec iov[kMaxIovLen];

  for (size_t i = 0; i < buffers.size(); ++i) {
    iov[i].iov_base = const_cast<char*>(buffers[i].data());
    iov[i].iov_len = buffers[i].size();
  }

  return IovecCursor(iov, buffers.size());
}

} // anonymous namespace

TEST(IovecCursor, construct) {
  std::string buf1 = "12345";
  std::string buf2 = "67890";
  auto cursor = getIovecCursor({ buf1, buf2 });

  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(10, cursor.totalLength());
  EXPECT_EQ(0, cursor.tell());
}

TEST(IovecCursor, basic) {
  std::string buf2 = "345";
  std::string buf3 = "6789";
  std::string buf1 = "012";
  auto cursor = getIovecCursor({ buf1, buf2, buf3 });

  char dest[10] = {'\0'};

  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("01", dest);

  cursor.advance(2);
  EXPECT_EQ(2, cursor.tell());
  EXPECT_TRUE(cursor.hasDataAvailable());

  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 3);
  EXPECT_STREQ("234", dest);

  cursor.advance(3);
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(5, cursor.tell());

  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 5);
  EXPECT_STREQ("56789", dest);

  cursor.advance(4);
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(9, cursor.tell());

  cursor.advance(1);
  EXPECT_FALSE(cursor.hasDataAvailable());
  EXPECT_EQ(10, cursor.tell());
}

TEST(IovecCursor, peek) {
  uint32_t int2 = 12379;
  uint64_t int1 = 1928374;
  uint16_t int3 = 187;
  std::string buf2(reinterpret_cast<char*>(&int2), sizeof(uint32_t));
  std::string buf3(reinterpret_cast<char*>(&int3), sizeof(uint16_t));
  std::string buf1(reinterpret_cast<char*>(&int1), sizeof(uint64_t));

  auto cursor = getIovecCursor({ buf1, buf2, buf3 });

  EXPECT_EQ(int1, cursor.peek<uint64_t>());

  cursor.advance(4);
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(4, cursor.tell());
  uint64_t expected = int1 >> 32 | static_cast<uint64_t>(int2) << 32;
  EXPECT_EQ(expected, cursor.peek<uint64_t>());

  cursor.advance(4);
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(8, cursor.tell());
  EXPECT_EQ(int2, cursor.peek<uint32_t>());

  cursor.advance(1);
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(9, cursor.tell());
  expected = int2 >> 8 | static_cast<uint64_t>(int3) << (32 - 8);
  EXPECT_EQ(expected, cursor.peek<uint32_t>());

  cursor.advance(3);
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(12, cursor.tell());
  EXPECT_EQ(int3, cursor.peek<uint16_t>());

  cursor.advance(2);
  EXPECT_FALSE(cursor.hasDataAvailable());
  EXPECT_EQ(14, cursor.tell());
}

TEST(IovecCursor, read) {
  uint32_t int2 = 12379;
  uint64_t int1 = 1928374;
  uint16_t int3 = 187;
  std::string buf2(reinterpret_cast<char*>(&int2), sizeof(uint32_t));
  std::string buf3(reinterpret_cast<char*>(&int3), sizeof(uint16_t));
  std::string buf1(reinterpret_cast<char*>(&int1), sizeof(uint64_t));

  auto cursor = getIovecCursor({ buf1, buf2, buf3 });
  EXPECT_EQ(14, cursor.totalLength());

  EXPECT_EQ(int1, cursor.read<uint64_t>());
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(8, cursor.tell());

  EXPECT_EQ(int2, cursor.read<uint32_t>());
  EXPECT_TRUE(cursor.hasDataAvailable());
  EXPECT_EQ(12, cursor.tell());

  EXPECT_EQ(int3, cursor.read<uint16_t>());
  EXPECT_FALSE(cursor.hasDataAvailable());
  EXPECT_EQ(14, cursor.tell());
}

TEST(IovecCursor, advance_retreat) {
  std::string buf1 = "12345";
  std::string buf2 = "67890";
  auto cursor = getIovecCursor({ buf1, buf2 });

  char dest[3] = {'\0'};

  cursor.advance(2);
  EXPECT_EQ(2, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("34", dest);

  cursor.retreat(1);
  EXPECT_EQ(1, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("23", dest);


  cursor.advance(3);
  EXPECT_EQ(4, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("56", dest);

  cursor.retreat(2);
  EXPECT_EQ(2, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("34", dest);


  cursor.advance(4);
  EXPECT_EQ(6, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("78", dest);

  cursor.retreat(3);
  EXPECT_EQ(3, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("45", dest);


  cursor.advance(5);
  EXPECT_EQ(8, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("90", dest);

  cursor.retreat(4);
  EXPECT_EQ(4, cursor.tell());
  cursor.peekInto(reinterpret_cast<uint8_t*>(dest), 2);
  EXPECT_STREQ("56", dest);
}

TEST(IovecCursor, advance_retreat_edge_cases) {
  std::string buf1 = "12345";
  std::string buf2 = "67890";
  auto cursor = getIovecCursor({ buf1, buf2 });

  cursor.seek(0);
  EXPECT_EQ(0, cursor.tell());

  cursor.advance(10);
  EXPECT_EQ(10, cursor.tell());

  cursor.retreat(10);
  EXPECT_EQ(0, cursor.tell());
}
