/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <sys/uio.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/carbon/test/Util.h"
#include "mcrouter/lib/carbon/test/gen/CarbonTest.h"

using namespace carbon::test::util;

using carbon::test::SimpleEnum;
using carbon::test::SimpleStruct;
using carbon::test::TestRequest;
using facebook::memcache::coalesceAndGetRange;

TEST(CarbonBasic, defaultConstructed) {
  TestRequest req;

  // key
  EXPECT_TRUE(req.key().empty());
  EXPECT_EQ(0, req.key().size());
  EXPECT_EQ("", req.key().fullKey());
  EXPECT_EQ("", req.key().routingKey());
  EXPECT_EQ("", req.key().routingPrefix());
  EXPECT_EQ("", req.key().keyWithoutRoute());

  // bool
  EXPECT_FALSE(req.testBool());
  // char
  EXPECT_EQ('\0', req.testChar());
  // enum member
  EXPECT_EQ(SimpleEnum::Twenty, req.testEnum());

  // int8_t
  EXPECT_EQ(0, req.testInt8());
  // int16_t
  EXPECT_EQ(0, req.testInt16());
  // int32_t
  EXPECT_EQ(0, req.testInt32());
  // int64_t
  EXPECT_EQ(0, req.testInt64());
  // uint8_t
  EXPECT_EQ(0, req.testUInt8());
  // uint16_t
  EXPECT_EQ(0, req.testUInt16());
  // uint32_t
  EXPECT_EQ(0, req.testUInt32());
  // uint64_t
  EXPECT_EQ(0, req.testUInt64());

  // float
  EXPECT_EQ(0.0, req.testFloat());
  // double
  EXPECT_EQ(0.0, req.testDouble());

  // string
  EXPECT_TRUE(req.testShortString().empty());
  EXPECT_TRUE(req.testLongString().empty());
  // IOBuf
  EXPECT_TRUE(req.testIobuf().empty());

  // Mixed-in member functions
  EXPECT_EQ(0, req.int32Member());
  EXPECT_TRUE(req.stringMember().empty());
  EXPECT_EQ(SimpleEnum::Twenty, req.enumMember());

  // Member struct
  EXPECT_EQ(0, req.testStruct().int32Member());
  EXPECT_TRUE(req.testStruct().stringMember().empty());
  EXPECT_EQ(SimpleEnum::Twenty, req.testStruct().enumMember());

  // List of strings
  EXPECT_TRUE(req.testList().empty());

  // fields generated for every request (will likely be removed in the future)
  EXPECT_EQ(0, req.exptime());
  EXPECT_EQ(0, req.flags());
}

TEST(CarbonBasic, setAndGet) {
  const folly::StringPiece keyPiece(
      "/region/cluster/abcdefghijklmnopqrstuvwxyz|#|afterhashstop");
  TestRequest req(keyPiece);

  // key
  const auto reqKeyPiece = req.key().fullKey();
  EXPECT_EQ(keyPiece, reqKeyPiece);
  EXPECT_EQ(keyPiece, req.key().fullKey());
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", req.key().routingKey().str());
  EXPECT_EQ("/region/cluster/", req.key().routingPrefix().str());
  EXPECT_EQ(
      "abcdefghijklmnopqrstuvwxyz|#|afterhashstop",
      req.key().keyWithoutRoute().str());

  // bool
  req.testBool() = true;
  EXPECT_TRUE(req.testBool());
  // char
  req.testChar() = 'A';
  EXPECT_EQ('A', req.testChar());
  // enum member
  req.testEnum() = SimpleEnum::Negative;
  EXPECT_EQ(SimpleEnum::Negative, req.testEnum());
  EXPECT_EQ(-92233, static_cast<int64_t>(req.testEnum()));

  // int8_t
  req.testInt8() = kMinInt8;
  EXPECT_EQ(kMinInt8, req.testInt8());
  // int16_t
  req.testInt16() = kMinInt16;
  EXPECT_EQ(kMinInt16, req.testInt16());
  // int32_t
  req.testInt32() = kMinInt32;
  EXPECT_EQ(kMinInt32, req.testInt32());
  // int64_t
  req.testInt64() = kMinInt64;
  EXPECT_EQ(kMinInt64, req.testInt64());
  // uint8_t
  req.testUInt8() = kMaxUInt8;
  EXPECT_EQ(kMaxUInt8, req.testUInt8());
  // uint16_t
  req.testUInt16() = kMaxUInt16;
  EXPECT_EQ(kMaxUInt16, req.testUInt16());
  // uint32_t
  req.testUInt32() = kMaxUInt32;
  EXPECT_EQ(kMaxUInt32, req.testUInt32());
  // uint64_t
  req.testUInt64() = kMaxUInt64;
  EXPECT_EQ(kMaxUInt64, req.testUInt64());

  // float
  req.testFloat() = 12345.789f;
  EXPECT_FLOAT_EQ(12345.789f, req.testFloat());
  // double
  req.testDouble() = 12345.789;
  EXPECT_DOUBLE_EQ(12345.789, req.testDouble());

  // string
  req.testShortString() = kShortString.str();
  EXPECT_EQ(kShortString, req.testShortString());
  req.testLongString() = longString();
  EXPECT_EQ(longString(), req.testLongString());
  // IOBuf
  folly::IOBuf iobuf(folly::IOBuf::COPY_BUFFER, longString());
  req.testIobuf() = iobuf;
  EXPECT_EQ(
      coalesceAndGetRange(iobuf).str(),
      coalesceAndGetRange(req.testIobuf()).str());

  std::vector<std::string> strings = {
      "abcdefg", "xyz", kShortString.str(), longString()};
  req.testList() = strings;
  EXPECT_EQ(strings, req.testList());
}

TEST(CarbonTest, serializeDeserialize) {
  // Fill in a request
  TestRequest outRequest("abcdefghijklmnopqrstuvwxyz");
  outRequest.testBool() = true;
  outRequest.testChar() = 'A';
  outRequest.testEnum() = SimpleEnum::Negative;
  outRequest.testInt8() = kMinInt8;
  outRequest.testInt16() = kMinInt16;
  outRequest.testInt32() = kMinInt32;
  outRequest.testInt64() = kMinInt64;
  outRequest.testUInt8() = kMaxUInt8;
  outRequest.testUInt16() = kMaxUInt16;
  outRequest.testUInt32() = kMaxUInt32;
  outRequest.testUInt64() = kMaxUInt64;
  outRequest.testFloat() = 12345.678f;
  outRequest.testDouble() = 12345.678;
  outRequest.testShortString() = kShortString.str();
  outRequest.testLongString() = longString();
  outRequest.testIobuf() =
      folly::IOBuf(folly::IOBuf::COPY_BUFFER, kShortString);
  // Member struct
  outRequest.testStruct().int32Member() = 12345;
  outRequest.testStruct().stringMember() = kShortString.str();
  outRequest.testStruct().enumMember() = SimpleEnum::One;
  // Mixed-in struct
  outRequest.asSimpleStruct().int32Member() = 12345;
  outRequest.asSimpleStruct().stringMember() = kShortString.str();
  outRequest.asSimpleStruct().enumMember() = SimpleEnum::One;
  // List of strings
  outRequest.testList() = {"abcdefg", "xyz", kShortString.str(), longString()};

  const auto inRequest = serializeAndDeserialize(outRequest);
  expectEqTestRequest(outRequest, inRequest);
}

TEST(CarbonTest, veryLongString) {
  constexpr uint32_t kVeryLongStringSize = 1 << 30;
  std::string veryLongString(kVeryLongStringSize, 'x');

  TestRequest outRequest(longString());
  outRequest.testLongString() = std::move(veryLongString);
  const auto inRequest = serializeAndDeserialize(outRequest);
  expectEqTestRequest(outRequest, inRequest);
  EXPECT_EQ(kVeryLongStringSize, inRequest.testLongString().length());
}

TEST(CarbonTest, veryLongIobuf) {
  constexpr uint32_t kVeryLongIobufSize = 1 << 30;
  folly::IOBuf veryLongIobuf(folly::IOBuf::CREATE, kVeryLongIobufSize);
  std::memset(veryLongIobuf.writableTail(), 'x', kVeryLongIobufSize);
  veryLongIobuf.append(kVeryLongIobufSize);

  TestRequest outRequest(longString());
  outRequest.testIobuf() = std::move(veryLongIobuf);
  const auto inRequest = serializeAndDeserialize(outRequest);
  expectEqTestRequest(outRequest, inRequest);
  EXPECT_EQ(kVeryLongIobufSize, inRequest.testIobuf().length());
}
