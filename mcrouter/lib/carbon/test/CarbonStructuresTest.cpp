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

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/carbon/test/Util.h"
#include "mcrouter/lib/carbon/test/gen/CarbonTest.h"

using namespace carbon::test::util;

using carbon::test::SimpleStruct;
using carbon::test::TestReply;
using carbon::test::TestRequest;
using carbon::test::TestRequestStringKey;
using carbon::test2::util::SimpleEnum;
using facebook::memcache::coalesceAndGetRange;

namespace {

constexpr auto kKeyLiteral =
    "/region/cluster/abcdefghijklmnopqrstuvwxyz|#|afterhashstop";

template <class Key>
void checkKeyEmpty(const Key& key) {
  const auto emptyRoutingKeyHash = TestRequest().key().routingKeyHash();

  EXPECT_TRUE(key.empty());
  EXPECT_EQ(0, key.size());
  EXPECT_EQ("", key.fullKey());
  EXPECT_EQ("", key.routingKey());
  EXPECT_EQ("", key.routingPrefix());
  EXPECT_EQ("", key.keyWithoutRoute());
  EXPECT_FALSE(key.hasHashStop());
  EXPECT_EQ(emptyRoutingKeyHash, key.routingKeyHash());
}

template <class Key>
void checkKeyFilledProperly(const Key& key) {
  EXPECT_FALSE(key.empty());
  EXPECT_EQ(std::strlen(kKeyLiteral), key.size());
  EXPECT_EQ(kKeyLiteral, key.fullKey());
  EXPECT_EQ(
      "abcdefghijklmnopqrstuvwxyz|#|afterhashstop", key.keyWithoutRoute());
  EXPECT_EQ("/region/cluster/", key.routingPrefix());
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", key.routingKey());
  EXPECT_NE(0, key.routingKeyHash());
  EXPECT_TRUE(key.hasHashStop());
}

} // anonymous

TEST(CarbonBasic, staticAsserts) {
  static_assert(!TestRequest::hasExptime, "");
  static_assert(!TestRequest::hasFlags, "");
  static_assert(TestRequest::hasKey, "");
  static_assert(!TestRequest::hasValue, "");
  static_assert(TestRequest::typeId == 69, "");

  static_assert(!TestReply::hasExptime, "");
  static_assert(!TestReply::hasFlags, "");
  static_assert(!TestReply::hasKey, "");
  static_assert(!TestReply::hasValue, "");
  static_assert(TestReply::typeId == 70, "");

  static_assert(carbon::IsCarbonStruct<TestRequest>::value, "");
  static_assert(!carbon::IsCarbonStruct<int>::value, "");
}

TEST(CarbonBasic, defaultConstructed) {
  TestRequest req;
  TestRequestStringKey req2;

  // key
  checkKeyEmpty(req.key());
  checkKeyEmpty(req2.key());

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
  EXPECT_EQ(0, req.baseInt64Member());

  // Member struct
  EXPECT_EQ(0, req.testStruct().int32Member());
  EXPECT_TRUE(req.testStruct().stringMember().empty());
  EXPECT_EQ(SimpleEnum::Twenty, req.testStruct().enumMember());

  // List of strings
  EXPECT_TRUE(req.testList().empty());

  // Vector of enums
  EXPECT_TRUE(req.testEnumVec().empty());

  // folly::Optional fields
  EXPECT_FALSE(req.testOptionalString());
  EXPECT_FALSE(req.testOptionalIobuf());

  // fields generated for every request (will likely be removed in the future)
  EXPECT_EQ(0, req.exptime());
  EXPECT_EQ(0, req.flags());
}

TEST(CarbonBasic, setAndGet) {
  TestRequest req(kKeyLiteral);
  TestRequestStringKey req2(kKeyLiteral);

  // key
  const auto reqKeyPiece = req.key().fullKey();
  EXPECT_EQ(kKeyLiteral, reqKeyPiece);
  EXPECT_EQ(kKeyLiteral, req.key().fullKey());
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", req.key().routingKey().str());
  EXPECT_EQ("/region/cluster/", req.key().routingPrefix().str());
  EXPECT_EQ(
      "abcdefghijklmnopqrstuvwxyz|#|afterhashstop",
      req.key().keyWithoutRoute().str());

  const auto reqKeyPiece2 = req2.key().fullKey();
  EXPECT_EQ(kKeyLiteral, reqKeyPiece2);
  EXPECT_EQ(kKeyLiteral, req2.key().fullKey());
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", req2.key().routingKey().str());
  EXPECT_EQ("/region/cluster/", req2.key().routingPrefix().str());
  EXPECT_EQ(
      "abcdefghijklmnopqrstuvwxyz|#|afterhashstop",
      req2.key().keyWithoutRoute().str());

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

  // Vector of enums
  std::vector<SimpleEnum> enums = {SimpleEnum::One,
                                   SimpleEnum::Zero,
                                   SimpleEnum::Twenty,
                                   SimpleEnum::Negative};
  req.testEnumVec() = enums;
  EXPECT_EQ(enums, req.testEnumVec());

  // folly::Optional fields
  const auto s = longString();
  req.testOptionalString() = s;
  EXPECT_EQ(s, *req.testOptionalString());
  req.testOptionalIobuf() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, s);
  EXPECT_EQ(s, coalesceAndGetRange(*req.testOptionalIobuf()));
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
  // Mixed-in structure accessors
  outRequest.int32Member() = 12345;
  outRequest.stringMember() = kShortString.str();
  outRequest.enumMember() = SimpleEnum::One;
  outRequest.baseInt64Member() = 12345;
  // List of strings
  outRequest.testList() = {"abcdefg", "xyz", kShortString.str(), longString()};
  // Force one optional field to not be serialized on the wire
  EXPECT_FALSE(outRequest.testOptionalString().hasValue());
  // Other optional field gets a value of zero length
  outRequest.testOptionalIobuf() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "");

  outRequest.testEnumVec().push_back(carbon::test2::util::SimpleEnum::Twenty);

  const auto inRequest = serializeAndDeserialize(outRequest);
  expectEqTestRequest(outRequest, inRequest);
}

TEST(CarbonTest, mixins) {
  TestRequest request;
  EXPECT_EQ(0, request.asBase().asBaseStruct().baseInt64Member());

  request.asBase().asBaseStruct().baseInt64Member() = 12345;
  // Exercise the different ways we can access the mixed-in baseInt64Member
  EXPECT_EQ(12345, request.asBase().asBaseStruct().baseInt64Member());
  EXPECT_EQ(12345, request.asBase().baseInt64Member());
  EXPECT_EQ(12345, request.asBaseStruct().baseInt64Member());
  EXPECT_EQ(12345, request.baseInt64Member());
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

TEST(CarbonTest, keysIobuf) {
  {
    TestRequest req;
    checkKeyEmpty(req.key());
  }
  {
    TestRequest req;

    const folly::IOBuf keyCopy(folly::IOBuf::CopyBufferOp(), kKeyLiteral);
    req.key() = keyCopy;
    checkKeyFilledProperly(req.key());

    req.key() = "";
    checkKeyEmpty(req.key());
  }
  {
    TestRequest req;
    checkKeyEmpty(req.key());

    req.key() = folly::IOBuf(folly::IOBuf::CopyBufferOp(), kKeyLiteral);
    checkKeyFilledProperly(req.key());
  }
  {
    TestRequest req(kKeyLiteral);
    checkKeyFilledProperly(req.key());
  }
  {
    TestRequest req{folly::StringPiece(kKeyLiteral)};
    checkKeyFilledProperly(req.key());
  }
  {
    TestRequest req(folly::IOBuf(folly::IOBuf::CopyBufferOp(), kKeyLiteral));
    checkKeyFilledProperly(req.key());
  }
}

TEST(CarbonTest, keysString) {
  {
    TestRequestStringKey req;
    checkKeyEmpty(req.key());
  }
  {
    TestRequestStringKey req;

    const std::string keyCopy(kKeyLiteral);
    req.key() = keyCopy;
    checkKeyFilledProperly(req.key());

    req.key() = "";
    checkKeyEmpty(req.key());
  }
  {
    TestRequestStringKey req;
    checkKeyEmpty(req.key());

    req.key() = kKeyLiteral;
    checkKeyFilledProperly(req.key());
  }
  {
    TestRequestStringKey req(kKeyLiteral);
    checkKeyFilledProperly(req.key());
  }
  {
    TestRequest req{folly::StringPiece(kKeyLiteral)};
    checkKeyFilledProperly(req.key());
  }
  {
    TestRequest req{std::string(kKeyLiteral)};
    checkKeyFilledProperly(req.key());
  }
}
