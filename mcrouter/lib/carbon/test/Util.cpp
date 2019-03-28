/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "mcrouter/lib/carbon/test/Util.h"

#include <gtest/gtest.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/carbon/test/gen/CarbonTest.h"
#include "mcrouter/lib/carbon/test/gen/CompactTest.h"

using facebook::memcache::coalesceAndGetRange;

namespace carbon {
namespace test {
namespace util {

std::string longString() {
  return std::string(1024, 'a');
}

namespace {
void compareOptionalIobuf(
    const folly::Optional<folly::IOBuf>& a,
    const folly::Optional<folly::IOBuf>& b) {
  EXPECT_EQ(a.hasValue(), b.hasValue());
  if (a.hasValue()) {
    folly::IOBuf aCopy = *a;
    folly::IOBuf bCopy = *b;
    EXPECT_EQ(coalesceAndGetRange(aCopy), coalesceAndGetRange(bCopy));
  }
}
} // namespace

void expectEqSimpleStruct(const SimpleStruct& a, const SimpleStruct& b) {
  EXPECT_EQ(a.int32Member(), b.int32Member());
  EXPECT_EQ(a.stringMember(), b.stringMember());
  EXPECT_EQ(a.enumMember(), b.enumMember());
}

void expectEqTestRequest(const TestRequest& a, const TestRequest& b) {
  EXPECT_EQ(a.key().fullKey(), b.key().fullKey());

  EXPECT_EQ(a.testBool(), b.testBool());
  EXPECT_EQ(a.testChar(), b.testChar());
  EXPECT_EQ(a.testEnum(), b.testEnum());

  EXPECT_EQ(a.testInt8(), b.testInt8());
  EXPECT_EQ(a.testInt16(), b.testInt16());
  EXPECT_EQ(a.testInt32(), b.testInt32());
  EXPECT_EQ(a.testInt64(), b.testInt64());

  EXPECT_EQ(a.testUInt8(), b.testUInt8());
  EXPECT_EQ(a.testUInt16(), b.testUInt16());
  EXPECT_EQ(a.testUInt32(), b.testUInt32());
  EXPECT_EQ(a.testUInt64(), b.testUInt64());

  EXPECT_FLOAT_EQ(a.testFloat(), b.testFloat());
  EXPECT_DOUBLE_EQ(a.testDouble(), b.testDouble());

  EXPECT_EQ(a.testShortString(), b.testShortString());
  EXPECT_EQ(a.testLongString(), b.testLongString());

  EXPECT_EQ(
      coalesceAndGetRange(const_cast<folly::IOBuf&>(a.testIobuf())),
      coalesceAndGetRange(const_cast<folly::IOBuf&>(b.testIobuf())));

  // Mixed-in structure
  EXPECT_EQ(a.int32Member(), b.int32Member());
  EXPECT_EQ(a.stringMember(), b.stringMember());
  EXPECT_EQ(a.enumMember(), b.enumMember());
  expectEqSimpleStruct(a.asBase(), b.asBase());

  // Member structure
  expectEqSimpleStruct(a.testStruct(), b.testStruct());

  EXPECT_EQ(a.testList(), b.testList());

  EXPECT_EQ(a.testNestedVec(), b.testNestedVec());

  EXPECT_EQ(a.testEnumVec(), b.testEnumVec());

  EXPECT_EQ(a.testUMap(), b.testUMap());
  EXPECT_EQ(a.testMap(), b.testMap());
  EXPECT_EQ(a.testF14FastMap(), b.testF14FastMap());
  EXPECT_EQ(a.testF14NodeMap(), b.testF14NodeMap());
  EXPECT_EQ(a.testF14ValueMap(), b.testF14ValueMap());
  EXPECT_EQ(a.testF14VectorMap(), b.testF14VectorMap());
  EXPECT_EQ(a.testComplexMap(), b.testComplexMap());

  EXPECT_EQ(a.testUSet(), b.testUSet());
  EXPECT_EQ(a.testSet(), b.testSet());
  EXPECT_EQ(a.testF14FastSet(), b.testF14FastSet());
  EXPECT_EQ(a.testF14NodeSet(), b.testF14NodeSet());
  EXPECT_EQ(a.testF14ValueSet(), b.testF14ValueSet());
  EXPECT_EQ(a.testF14VectorSet(), b.testF14VectorSet());

  EXPECT_EQ(a.testOptionalString(), b.testOptionalString());
  compareOptionalIobuf(a.testOptionalIobuf(), b.testOptionalIobuf());

  EXPECT_EQ(a.testType().name, b.testType().name);
  EXPECT_EQ(a.testType().points, b.testType().points);

  EXPECT_EQ(a.testOptionalBool(), b.testOptionalBool());
  EXPECT_EQ(a.testOptionalVec(), b.testOptionalVec());
}

void expectEqTestCompactRequest(
    const TestCompactRequest& a,
    const TestCompactRequest& b) {
  EXPECT_EQ(a.key().fullKey(), b.key().fullKey());

  EXPECT_EQ(a.testBool(), b.testBool());
  EXPECT_EQ(a.testChar(), b.testChar());
  EXPECT_EQ(a.testEnum(), b.testEnum());

  EXPECT_EQ(a.testInt8(), b.testInt8());
  EXPECT_EQ(a.testInt16(), b.testInt16());
  EXPECT_EQ(a.testInt32(), b.testInt32());
  EXPECT_EQ(a.testInt64(), b.testInt64());

  EXPECT_EQ(a.testUInt8(), b.testUInt8());
  EXPECT_EQ(a.testUInt16(), b.testUInt16());
  EXPECT_EQ(a.testUInt32(), b.testUInt32());
  EXPECT_EQ(a.testUInt64(), b.testUInt64());

  EXPECT_EQ(a.testShortString(), b.testShortString());
  EXPECT_EQ(a.testLongString(), b.testLongString());

  EXPECT_EQ(
      coalesceAndGetRange(const_cast<folly::IOBuf&>(a.testIobuf())),
      coalesceAndGetRange(const_cast<folly::IOBuf&>(b.testIobuf())));
  EXPECT_EQ(a.testList(), b.testList());
}

} // namespace util
} // namespace test
} // namespace carbon
