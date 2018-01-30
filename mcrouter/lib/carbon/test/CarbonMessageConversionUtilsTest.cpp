/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/json.h>

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
  r.testUnion().emplace<2>(true);
  r.testNestedVec() = {{1, 1, 1}, {2, 2, 2}};
  r.testUMap() = std::unordered_map<std::string, std::string>(
      {{"key", "value"}, {"adele", "beyonce"}});
  r.testMap() = std::map<double, double>({{3.14, 2.7}, {0.577, 0.2}});
  r.testComplexMap() = std::map<std::string, std::vector<uint16_t>>(
      {{"hello", {1, 1, 1}}, {"world", {2, 2, 2}}});
  r.testUSet() = std::unordered_set<std::string>({"hello", "world"});
  r.testSet() = std::set<uint64_t>({123, 456});

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
      "testEnumVec", folly::dynamic::array(1, 20))(
      "testUnion", folly::dynamic::object("umember2", true))(
      "testNestedVec",
      folly::dynamic::array(
          folly::dynamic::array(1, 1, 1), folly::dynamic::array(2, 2, 2)))(
      "testUMap", folly::dynamic::object("key", "value")("adele", "beyonce"))(
      "testMap", folly::dynamic::object("3.14", 2.7)("0.577", 0.2))(
      "testComplexMap",
      folly::dynamic::object("hello", folly::dynamic::array(1, 1, 1))(
          "world", folly::dynamic::array(2, 2, 2)))(
      "testUSet", folly::dynamic::array("hello", "world"))(
      "testSet", folly::dynamic::array(123, 456))("testType", "(user type)")(
      "testOptionalVec", folly::dynamic::array());

  auto dynamic = carbon::convertToFollyDynamic(r);
  auto set = dynamic.at("testUSet");
  std::sort(set.begin(), set.end());
  dynamic.at("testUSet") = set;

  EXPECT_EQ(expected, dynamic);
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

TEST(CarbonMessageConversionUtils, fromFollyDynamic_InlineMixins) {
  const std::string jsonStr = R"json(
    {
      "int32Member": 32,
      "stringMember": "This is a string",
      "baseInt64Member": 132
    }
  )json";

  carbon::test::SimpleStruct s;
  carbon::convertFromFollyDynamic(folly::parseJson(jsonStr), s);

  EXPECT_EQ(32, s.int32Member());
  EXPECT_EQ("This is a string", s.stringMember());
  EXPECT_EQ(132, s.baseInt64Member());
}

TEST(CarbonMessageConversionUtils, fromFollyDynamic) {
  const std::string jsonStr = R"json(
    {
      "int32Member": 32,
      "stringMember": "This is a string",
      "__BaseStruct": {
        "baseInt64Member": 132
      }
    }
  )json";

  carbon::test::SimpleStruct s;
  carbon::convertFromFollyDynamic(folly::parseJson(jsonStr), s);

  EXPECT_EQ(32, s.int32Member());
  EXPECT_EQ("This is a string", s.stringMember());
  EXPECT_EQ(132, s.baseInt64Member());
}

TEST(CarbonMessageConversionUtils, fromFollyDynamic_Complex) {
  const std::string jsonStr = R"json(
    {
      "key": "sampleKey",

      "int32Member": 32,
      "stringMember": "This is a string",
      "__BaseStruct": {
        "baseInt64Member": 132
      },

      "testEnum": -92233,
      "testBool": true,
      "testChar": "a",

      "testInt8": -8,
      "testInt16": -16,
      "testInt32": -32,
      "testInt64": -64,
      "testUInt8": 8,
      "testUInt16": 16,
      "testUInt32": 32,
      "testUInt64": 64,

      "testFloat": 12.356,
      "testDouble": 35.98765,

      "testLongString": "this is a very long and nice string in a json file 12",
      "testIobuf": "iobuf string here...",

      "testStruct": {
        "int32Member": 7,
        "stringMember": "I'm nested!",
        "baseInt64Member": 9
      },

      "testList": [
        "string 1",
        "s2"
      ],

      "testOptionalString": "I exist!",

      "testUnion": {
        "umember3": "abc def ghi"
      },

      "testNestedVec": [
        [ 17, 26 ],
        [],
        [ 32 ]
      ],

      "testMap": {
        10.7: 11.8,
        30.567: 31.789
      },

      "testComplexMap": {
        "v1": [ 10 ],
        "ve2": [ 20, 30 ],
        "vec03": [ 50, 70, 90 ]
      }
    }
  )json";

  size_t numErrors = 0;
  auto onError = [&numErrors](folly::StringPiece name, folly::StringPiece msg) {
    std::cerr << "ERROR: " << name << ": " << msg << std::endl;
    numErrors++;
  };

  folly::json::serialization_opts jsonOpts;
  jsonOpts.allow_non_string_keys = true;
  folly::dynamic json = folly::parseJson(jsonStr, jsonOpts);

  carbon::test::TestRequest r;
  carbon::convertFromFollyDynamic(json, r, std::move(onError));

  EXPECT_EQ(0, numErrors);

  EXPECT_EQ("sampleKey", r.key().fullKey());

  // Simple struct
  EXPECT_EQ(32, r.int32Member());
  EXPECT_EQ("This is a string", r.stringMember());
  EXPECT_EQ(132, r.baseInt64Member());

  EXPECT_EQ(carbon::test2::util::SimpleEnum::Negative, r.testEnum());
  EXPECT_TRUE(r.testBool());
  EXPECT_EQ('a', r.testChar());

  EXPECT_EQ(-8, r.testInt8());
  EXPECT_EQ(-16, r.testInt16());
  EXPECT_EQ(-32, r.testInt32());
  EXPECT_EQ(-64, r.testInt64());
  EXPECT_EQ(8, r.testUInt8());
  EXPECT_EQ(16, r.testUInt16());
  EXPECT_EQ(32, r.testUInt32());
  EXPECT_EQ(64, r.testUInt64());

  EXPECT_FLOAT_EQ(12.356, r.testFloat());
  EXPECT_DOUBLE_EQ(35.98765, r.testDouble());

  EXPECT_EQ(
      "this is a very long and nice string in a json file 12",
      r.testLongString());
  const folly::IOBuf expectedIobuf(
      folly::IOBuf::CopyBufferOp(), folly::StringPiece("iobuf string here..."));
  EXPECT_TRUE(folly::IOBufEqual()(expectedIobuf, r.testIobuf()));

  EXPECT_EQ(7, r.testStruct().int32Member());
  EXPECT_EQ("I'm nested!", r.testStruct().stringMember());
  EXPECT_EQ(9, r.testStruct().baseInt64Member());

  ASSERT_EQ(2, r.testList().size());
  EXPECT_EQ("string 1", r.testList()[0]);
  EXPECT_EQ("s2", r.testList()[1]);

  ASSERT_TRUE(r.testOptionalString().hasValue());
  EXPECT_EQ("I exist!", r.testOptionalString().value());

  ASSERT_EQ(3, r.testUnion().which());
  EXPECT_EQ("abc def ghi", r.testUnion().umember3());

  ASSERT_EQ(3, r.testNestedVec().size());
  ASSERT_EQ(2, r.testNestedVec()[0].size());
  EXPECT_EQ(0, r.testNestedVec()[1].size());
  ASSERT_EQ(1, r.testNestedVec()[2].size());
  EXPECT_EQ(17, r.testNestedVec()[0][0]);
  EXPECT_EQ(26, r.testNestedVec()[0][1]);
  EXPECT_EQ(32, r.testNestedVec()[2][0]);

  ASSERT_EQ(2, r.testMap().size());
  EXPECT_EQ(11.8, r.testMap()[10.7]);
  EXPECT_EQ(31.789, r.testMap()[30.567]);

  ASSERT_EQ(3, r.testComplexMap().size());
  ASSERT_EQ(1, r.testComplexMap()["v1"].size());
  EXPECT_EQ(10, r.testComplexMap()["v1"][0]);
  ASSERT_EQ(2, r.testComplexMap()["ve2"].size());
  EXPECT_EQ(20, r.testComplexMap()["ve2"][0]);
  EXPECT_EQ(30, r.testComplexMap()["ve2"][1]);
  ASSERT_EQ(3, r.testComplexMap()["vec03"].size());
  EXPECT_EQ(50, r.testComplexMap()["vec03"][0]);
  EXPECT_EQ(70, r.testComplexMap()["vec03"][1]);
  EXPECT_EQ(90, r.testComplexMap()["vec03"][2]);
}

TEST(CarbonMessageConversionUtils, fromFollyDynamic_Errors) {
  const std::string jsonStr = R"json(
    {
      "key": 75,

      "int32Member": "abc",

      "testChar": "ab",

      "testStruct": {
        "__BaseStruct": {
          "baseInt64Member": "abc"
        }
      },

      "testList": [
        "string 1",
        7
      ],

      "testUnion": {
        "umember2": 17
      },

      "testNestedVec": [
        [],
        [ 18, "abc" ]
      ]
    }
  )json";

  size_t numErrors = 0;
  auto onError = [&numErrors](
      folly::StringPiece fieldName, folly::StringPiece msg) {
    numErrors++;
    std::cerr << fieldName << ": " << msg << std::endl;
  };

  carbon::test::TestRequest r;
  carbon::convertFromFollyDynamic(
      folly::parseJson(jsonStr), r, std::move(onError));

  EXPECT_EQ(7, numErrors);

  ASSERT_EQ(1, r.testList().size());
  EXPECT_EQ("string 1", r.testList()[0]);

  ASSERT_EQ(2, r.testNestedVec().size());
  EXPECT_EQ(0, r.testNestedVec()[0].size());
  ASSERT_EQ(1, r.testNestedVec()[1].size());
  EXPECT_EQ(18, r.testNestedVec()[1][0]);
}
