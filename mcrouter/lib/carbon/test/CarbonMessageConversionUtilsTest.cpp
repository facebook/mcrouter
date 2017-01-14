/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/carbon/CarbonMessageConversionUtils.h"
#include "mcrouter/lib/carbon/test/gen/CarbonTest.h"

TEST(CarbonMessageConversionUtils, toFollyDynamic_Complex) {
  carbon::test::TestRequest r;
  r.baseInt64Member() = 1;
  r.int32Member() = -1;
  r.stringMember() = "testStrMbr";
  r.enumMember() = carbon::test2::util::SimpleEnum::One;
  r.vectorMember().emplace_back();
  r.vectorMember().back().member1() = 342;
  r.vectorMember().emplace_back();
  r.vectorMember().back().member1() = 123;
  r.key() = carbon::Keys<folly::IOBuf>("/test/key/");
  r.testEnum() = carbon::test2::util::SimpleEnum::Negative;
  r.testBool() = true;
  r.testChar() = 'a';
  r.testInt8() = -123;
  r.testInt16() = -7890;
  r.testInt32() = -123456789;
  r.testInt64() = -9876543210123ll;
  r.testUInt8() = 123;
  r.testUInt16() = 7890;
  r.testUInt32() = 123456789;
  r.testUInt64() = 9876543210123ll;
  r.testFloat() = 1.5;
  r.testDouble() = 5.6;
  r.testShortString() = "abcdef";
  r.testLongString() = "asdfghjkl;'eqtirgwuifhiivlzkhbvjkhc3978y42h97*&687gba";
  r.testIobuf() = folly::IOBuf(
      folly::IOBuf::CopyBufferOp(), folly::StringPiece("TestTheBuf"));
  r.testStruct().baseInt64Member() = 345;
  r.testStruct().stringMember() = "nestedSimpleStruct";
  r.testOptionalString() = "tstOptStr";
  r.testList() = std::vector<std::string>({"abc", "bce", "xyz"});
  r.testEnumVec() = std::vector<carbon::test2::util::SimpleEnum>(
      {carbon::test2::util::SimpleEnum::One,
       carbon::test2::util::SimpleEnum::Twenty});

  folly::dynamic expected = folly::dynamic::object(
      "__Base",
      folly::dynamic::object(
          "__BaseStruct", folly::dynamic::object("baseInt64Member", 1))(
          "int32Member", -1)("stringMember", "testStrMbr")("enumMember", 1)(
          "vectorMember",
          folly::dynamic::array(
              folly::dynamic::object("member1", 342),
              folly::dynamic::object("member1", 123))))("key", "/test/key/")(
      "testEnum", -92233)("testBool", true)("testChar", "a")("testInt8", -123)(
      "testInt16", -7890)("testInt32", -123456789)(
      "testInt64", -9876543210123ll)("testUInt8", 123)("testUInt16", 7890)(
      "testUInt32", 123456789)("testUInt64", 9876543210123ll)("testFloat", 1.5)(
      "testDouble", 5.6)("testShortString", "abcdef")(
      "testLongString",
      "asdfghjkl;'eqtirgwuifhiivlzkhbvjkhc3978y42h97*&687gba")(
      "testIobuf", "TestTheBuf")(
      "testStruct",
      folly::dynamic::object(
          "__BaseStruct", folly::dynamic::object("baseInt64Member", 345))(
          "enumMember", 20)("int32Member", 0)(
          "stringMember", "nestedSimpleStruct")(
          "vectorMember", folly::dynamic::array()))(
      "testOptionalString", "tstOptStr")(
      "testList", folly::dynamic::array("abc", "bce", "xyz"))(
      "testEnumVec", folly::dynamic::array(1, 20));

  EXPECT_EQ(expected, carbon::convertToFollyDynamic(r));
}

TEST(CarbonMessageConversionUtils, toFollyDynamic_InlineMixins) {
  carbon::test::SimpleStruct s;
  s.baseInt64Member() = 123;
  s.stringMember() = "abcdef";
  folly::dynamic noInline = folly::dynamic::object(
      "__BaseStruct", folly::dynamic::object("baseInt64Member", 123))(
      "int32Member", 0)("stringMember", "abcdef")("enumMember", 20)(
      "vectorMember", folly::dynamic::array());
  EXPECT_EQ(noInline, carbon::convertToFollyDynamic(s));
  folly::dynamic withInline = folly::dynamic::object("baseInt64Member", 123)(
      "int32Member", 0)("stringMember", "abcdef")("enumMember", 20)(
      "vectorMember", folly::dynamic::array());
  carbon::FollyDynamicConversionOptions opts;
  opts.inlineMixins = true;
  EXPECT_EQ(withInline, carbon::convertToFollyDynamic(s, opts));
}
