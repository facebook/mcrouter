/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <folly/json/dynamic.h>

#include "mcrouter/lib/fbi/cpp/util.h"

using facebook::memcache::checkLogic;
using facebook::memcache::checkRuntime;
using facebook::memcache::throwLogic;
using facebook::memcache::throwRuntime;
using facebook::memcache::detail::formatMessage;

// The expected strings are what folly::sformat("{}", d) produced for scalars.
// folly::sformat threw for arrays and objects; they print as JSON now.
TEST(UtilTest, formatsDynamicLikeFollyFormat) {
  EXPECT_EQ("(null)", formatMessage("{}", folly::dynamic(nullptr)));
  EXPECT_EQ("true", formatMessage("{}", folly::dynamic(true)));
  EXPECT_EQ("false", formatMessage("{}", folly::dynamic(false)));
  EXPECT_EQ("-42", formatMessage("{}", folly::dynamic(-42)));
  EXPECT_EQ("1.5", formatMessage("{}", folly::dynamic(1.5)));
  EXPECT_EQ("0.1", formatMessage("{}", folly::dynamic(0.1)));
  EXPECT_EQ("1e+21", formatMessage("{}", folly::dynamic(1e21)));
  EXPECT_EQ("abc", formatMessage("{}", folly::dynamic("abc")));
  EXPECT_EQ(
      "[1,\"a\"]",
      formatMessage("{}", folly::dynamic(folly::dynamic::array(1, "a"))));
  EXPECT_EQ(
      "{\"k\":\"v\"}",
      formatMessage("{}", folly::dynamic(folly::dynamic::object("k", "v"))));
}

TEST(UtilTest, appliesFormatSpecToDynamicScalar) {
  EXPECT_EQ("   abc", formatMessage("{:>6}", folly::dynamic("abc")));
  EXPECT_EQ("00042", formatMessage("{:05}", folly::dynamic(42)));
  EXPECT_EQ("2a", formatMessage("{:x}", folly::dynamic(42)));
  EXPECT_EQ("1.50", formatMessage("{:.2f}", folly::dynamic(1.5)));
  EXPECT_EQ("(null)  ", formatMessage("{:8}", folly::dynamic(nullptr)));
}

TEST(UtilTest, formatsDynamicReferencesAndMixedArgs) {
  const folly::dynamic obj = folly::dynamic::object("name", "pool")("n", 3);
  EXPECT_EQ(
      "pool has 3 servers",
      formatMessage("{} has {} servers", obj["name"], obj["n"]));
  EXPECT_EQ("x=7 y=pool", formatMessage("x={} y={}", 7, obj["name"]));
}

TEST(UtilTest, formatsNullCStringLikeFollyFormat) {
  const char* null = nullptr;
  const char* str = "abc";
  char buf[] = "xyz";
  EXPECT_EQ("(null) abc xyz", formatMessage("{} {} {}", null, str, buf));
  EXPECT_EQ("   abc", formatMessage("{:>6}", str));
}

TEST(UtilTest, throwsFormattedMessage) {
  const folly::dynamic name = "pool";
  try {
    throwLogic("bad {}: {}", name, 5);
    FAIL();
  } catch (const std::logic_error& e) {
    EXPECT_STREQ("bad pool: 5", e.what());
  }
  try {
    throwRuntime("bad {}", std::string("x"));
    FAIL();
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ("bad x", e.what());
  }
  EXPECT_THROW(checkLogic(false, "{{literal}} {}", 1), std::logic_error);
  EXPECT_THROW(checkRuntime(false, "no args"), std::runtime_error);
  checkLogic(true, "{}", name);
  checkRuntime(true, "{}", 1);
}
