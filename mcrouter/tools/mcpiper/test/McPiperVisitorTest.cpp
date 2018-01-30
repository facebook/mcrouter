/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/carbon/test/gen/CarbonTestMessages.h"
#include "mcrouter/lib/network/gen/MemcacheMessages.h"
#include "mcrouter/tools/mcpiper/McPiperVisitor.h"

using carbon::detail::McPiperVisitor;
using namespace facebook::memcache;
using namespace carbon::test;

namespace {

void testBasic(bool scriptMode) {
  McLeaseSetReply msg(mc_res_found);
  msg.appSpecificErrorCode() = 17;
  msg.message() = "A message";

  McPiperVisitor v(scriptMode);
  msg.visitFields(v);

  auto str = std::move(v).styled();

  EXPECT_TRUE(str.text().contains("A message"));
  EXPECT_TRUE(str.text().contains("17"));
}

void testComplete(bool scriptMode) {
  TestRequest msg("abc");
  msg.testList() = {"qqq", "www"};
  msg.testUMap() = {{"abc", "def"}, {"a", "b"}};
  msg.testComplexMap() = {
      {"key01", {1, 2, 3}}, {"key02", {5, 6}}, {"key03", {}}};
  msg.testUnion().umember1() = 888;
  msg.testOptionalVec().emplace_back("OptionalString1");
  msg.testOptionalVec().push_back(folly::none);
  msg.testOptionalVec().emplace_back("OptionalString3");

  McPiperVisitor v(scriptMode);
  msg.visitFields(v);

  auto str = std::move(v).styled();

  EXPECT_TRUE(str.text().contains("qqq"));
  EXPECT_TRUE(str.text().contains("www"));

  EXPECT_TRUE(str.text().contains("abc"));
  EXPECT_TRUE(str.text().contains("def"));

  EXPECT_TRUE(str.text().contains("key01"));
  EXPECT_TRUE(str.text().contains("5"));

  EXPECT_TRUE(str.text().contains("888"));

  EXPECT_TRUE(str.text().contains("OptionalString1"));
  EXPECT_TRUE(str.text().contains("OptionalString3"));
}

} // anonymous namespace

TEST(McPiperVisitor, basic) {
  testBasic(false /* scriptMode */);
}
TEST(McPiperVisitor, basic_script) {
  testBasic(true /* scriptMode */);
}

TEST(McPiperVisitor, complete) {
  testComplete(false /* scriptMode */);
}
TEST(McPiperVisitor, complete_script) {
  testComplete(true /* scriptMode */);
}
