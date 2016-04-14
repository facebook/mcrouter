/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/allocator/JemallocNodumpAllocator.h"

#ifdef CAN_USE_JEMALLOC_NODUMP_ALLOCATOR

#include <gtest/gtest.h>
#include <folly/io/IOBuf.h>

using namespace testing;

class JemallocNodumpAllocatorTest : public Test {
 public:
  void SetUp() override {}
  void TearDown() override {}

 protected:
  facebook::memcache::JemallocNodumpAllocator jna;
};

TEST_F(JemallocNodumpAllocatorTest, basic) {
  EXPECT_NE(
    nullptr,
    facebook::memcache::JemallocNodumpAllocator::getOriginalChunkAlloc());
  EXPECT_NE(nullptr, jna.allocate(1024));
}

TEST_F(JemallocNodumpAllocatorTest, folly) {
  EXPECT_NE(
    nullptr,
    facebook::memcache::JemallocNodumpAllocator::getOriginalChunkAlloc());
  const size_t size{1024};
  void* ptr = jna.allocate(size);
  EXPECT_NE(nullptr, ptr);
  folly::IOBuf ioBuf(folly::IOBuf::TAKE_OWNERSHIP, ptr, size);
  EXPECT_EQ(size, ioBuf.capacity());
  EXPECT_EQ(ptr, ioBuf.data());
  uint8_t* data = ioBuf.writableData();
  EXPECT_EQ(ptr, data);
  for (auto i = 0u; i < ioBuf.capacity(); ++i) {
    data[i] = 'A';
  }
  uint8_t* p = static_cast<uint8_t*> (ptr);
  for (auto i = 0u; i < size; ++i) {
    EXPECT_EQ('A', p[i]);
  }
}
#endif
