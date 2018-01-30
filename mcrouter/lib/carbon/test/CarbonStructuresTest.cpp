/*
 *  Copyright (c) 2016-present, Facebook, Inc.
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

  // Vector of vectors
  EXPECT_TRUE(req.testNestedVec().empty());

  // folly::Optional fields
  EXPECT_FALSE(req.testOptionalString());
  EXPECT_FALSE(req.testOptionalIobuf());

  // Unordered map
  EXPECT_TRUE(req.testUMap().empty());

  // Ordered map
  EXPECT_TRUE(req.testMap().empty());

  // Complex map
  EXPECT_TRUE(req.testComplexMap().empty());

  // Unordered set
  EXPECT_TRUE(req.testUSet().empty());

  // Ordered set
  EXPECT_TRUE(req.testSet().empty());

  // User Type
  EXPECT_TRUE(carbon::SerializationTraits<carbon::test::UserType>::isEmpty(
      req.testType()));

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

  // Union
  req.testUnion().emplace<2>(true);
  bool b = req.testUnion().get<2>();
  EXPECT_TRUE(b);
  req.testUnion().emplace<1>(123);
  EXPECT_EQ(123, req.testUnion().get<1>());
  req.testUnion().emplace<3>("a");
  EXPECT_EQ("a", req.testUnion().get<3>());

  // Vector of vectors
  std::vector<std::vector<uint64_t>> vectors = {{1, 1, 1}, {2, 2}};
  req.testNestedVec() = vectors;
  EXPECT_EQ(vectors, req.testNestedVec());

  // folly::Optional fields
  const auto s = longString();
  req.testOptionalString() = s;
  EXPECT_EQ(s, *req.testOptionalString());
  req.testOptionalIobuf() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, s);
  EXPECT_EQ(s, coalesceAndGetRange(*req.testOptionalIobuf()));
  req.testOptionalBool() = false;
  EXPECT_EQ(false, *req.testOptionalBool());
  std::vector<folly::Optional<std::string>> ovec;
  ovec.emplace_back(folly::Optional<std::string>("hello"));
  req.testOptionalVec() = ovec;
  EXPECT_EQ(ovec, req.testOptionalVec());

  // Unordered map
  std::unordered_map<std::string, std::string> stringmap;
  stringmap.insert({"key", "value"});
  req.testUMap() = stringmap;
  EXPECT_EQ(stringmap, req.testUMap());

  // Ordered map
  std::map<double, double> doublemap;
  doublemap.insert({1.08, 8.3});
  req.testMap() = doublemap;
  EXPECT_EQ(doublemap, req.testMap());

  // Complex map
  std::map<std::string, std::vector<uint16_t>> complexmap;
  complexmap.insert({"key", {1, 2}});
  req.testComplexMap() = complexmap;
  EXPECT_EQ(complexmap, req.testComplexMap());

  // Unordered set
  std::unordered_set<std::string> stringset;
  stringset.insert("hello");
  stringset.insert("world");
  req.testUSet() = stringset;
  EXPECT_EQ(stringset, req.testUSet());

  // Ordered set
  std::set<uint64_t> intset;
  intset.insert(1);
  intset.insert(2);
  req.testSet() = intset;
  EXPECT_EQ(intset, req.testSet());

  // User type
  carbon::test::UserType testType = {"blah", {1, 2, 3}};
  req.testType() = testType;
  EXPECT_EQ(testType.name, req.testType().name);
  EXPECT_EQ(testType.points, req.testType().points);
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
  // Union
  outRequest.testUnion().emplace<3>("abc");

  outRequest.testEnumVec().push_back(carbon::test2::util::SimpleEnum::Twenty);

  outRequest.testNestedVec().push_back({100, 2000});

  outRequest.testUMap().insert({"hello", "world"});
  outRequest.testMap().insert({1.08, 8.3});
  outRequest.testComplexMap().insert({"key", {1, 2}});

  outRequest.testUSet().insert("hello");
  outRequest.testSet().insert(123);

  outRequest.testType() = {"blah", {1, 2, 3}};

  outRequest.testOptionalBool() = false;

  outRequest.testOptionalVec().emplace_back(folly::Optional<std::string>(""));
  outRequest.testOptionalVec().emplace_back(folly::Optional<std::string>());
  outRequest.testOptionalVec().emplace_back(
      folly::Optional<std::string>("hello"));

  const auto inRequest = serializeAndDeserialize(outRequest);
  expectEqTestRequest(outRequest, inRequest);
}

TEST(CarbonTest, unionZeroSerialization) {
  TestRequest outRequest;
  outRequest.testUnion().emplace<1>(0);
  auto inRequest = serializeAndDeserialize(outRequest);
  EXPECT_EQ(0, inRequest.testUnion().get<1>());

  outRequest.testUnion().emplace<3>("");
  inRequest = serializeAndDeserialize(outRequest);
  EXPECT_EQ("", inRequest.testUnion().get<3>());
}

TEST(CarbonTest, OptionalUnionSerialization) {
  carbon::test::TestOptionalUnion testOpt;
  testOpt.emplace<1>(1);
  auto inOpt = serializeAndDeserialize(testOpt);
  ASSERT_EQ(1, inOpt.which());
  EXPECT_EQ(testOpt.umember1().hasValue(), inOpt.umember1().hasValue());
  EXPECT_EQ(1, inOpt.umember1().value());

  testOpt.emplace<1>(folly::Optional<int64_t>());
  inOpt = serializeAndDeserialize(testOpt);
  ASSERT_EQ(1, inOpt.which());
  EXPECT_EQ(testOpt.umember1().hasValue(), inOpt.umember1().hasValue());

  testOpt.emplace<2>(folly::Optional<bool>(false));
  inOpt = serializeAndDeserialize(testOpt);
  ASSERT_EQ(2, inOpt.which());
  EXPECT_EQ(testOpt.umember2().hasValue(), inOpt.umember2().hasValue());
  EXPECT_EQ(false, inOpt.umember2().value());

  testOpt.emplace<2>(folly::Optional<bool>());
  inOpt = serializeAndDeserialize(testOpt);
  ASSERT_EQ(2, inOpt.which());
  EXPECT_EQ(testOpt.umember2().hasValue(), inOpt.umember2().hasValue());

  testOpt.emplace<3>(folly::Optional<std::string>("test"));
  inOpt = serializeAndDeserialize(testOpt);
  ASSERT_EQ(3, inOpt.which());
  EXPECT_EQ(testOpt.umember3().hasValue(), inOpt.umember3().hasValue());
  EXPECT_EQ("test", inOpt.umember3().value());

  testOpt.emplace<3>(folly::Optional<std::string>());
  inOpt = serializeAndDeserialize(testOpt);
  ASSERT_EQ(3, inOpt.which());
  EXPECT_EQ(testOpt.umember3().hasValue(), inOpt.umember3().hasValue());
}

TEST(CarbonTest, OptionalBoolSerializationBytesWritten) {
  carbon::test::TestOptionalBool testOpt;
  size_t bytesWritten;
  folly::Optional<bool> opt;
  testOpt.optionalBool() = opt;
  auto inOpt = serializeAndDeserialize(testOpt, bytesWritten);
  EXPECT_EQ(testOpt.optionalBool(), inOpt.optionalBool());
  EXPECT_EQ(1, bytesWritten); // One byte written for FieldType::Stop

  bytesWritten = 0;
  testOpt.optionalBool() = true;
  inOpt = serializeAndDeserialize(testOpt, bytesWritten);
  EXPECT_EQ(testOpt.optionalBool(), inOpt.optionalBool());
  EXPECT_EQ(2, bytesWritten);

  bytesWritten = 0;
  testOpt.optionalBool() = false;
  inOpt = serializeAndDeserialize(testOpt, bytesWritten);
  EXPECT_EQ(testOpt.optionalBool(), inOpt.optionalBool());
  EXPECT_EQ(2, bytesWritten);
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
