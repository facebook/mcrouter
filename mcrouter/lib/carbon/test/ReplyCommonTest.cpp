/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <type_traits>

#include <gtest/gtest.h>

#include "mcrouter/lib/carbon/ReplyCommon.h"

namespace carbon {
namespace {

static_assert(std::is_nothrow_default_constructible_v<ReplyCommon>);
static_assert(std::is_copy_constructible_v<ReplyCommon>);
static_assert(std::is_copy_assignable_v<ReplyCommon>);
static_assert(std::is_nothrow_move_constructible_v<ReplyCommon>);
static_assert(std::is_nothrow_move_assignable_v<ReplyCommon>);

FailoverAttempt attempt(std::string destination) {
  FailoverAttempt attempt;
  attempt.destination = std::move(destination);
  attempt.result = Result::BUSY;
  return attempt;
}

TEST(ReplyCommonTest, TracingStateIsEmptyByDefault) {
  ReplyCommon reply;

  EXPECT_TRUE(reply.artilleryServerBlockReport().empty());
  EXPECT_TRUE(reply.failoverAttempts().empty());
}

TEST(ReplyCommonTest, SettingOneTracingFieldKeepsTheOther) {
  ReplyCommon reply;
  reply.setArtilleryServerBlockReport("report");
  reply.setFailoverAttempts({attempt("a:11211")});

  reply.setArtilleryServerBlockReport("newer report");

  EXPECT_EQ(reply.artilleryServerBlockReport(), "newer report");
  ASSERT_EQ(reply.failoverAttempts().size(), 1);
  EXPECT_EQ(reply.failoverAttempts()[0].destination, "a:11211");
}

// Copies share the tracing state, so a write to one must not show in the other.
TEST(ReplyCommonTest, SettingOnACopyLeavesTheOriginalUnchanged) {
  ReplyCommon original;
  original.setArtilleryServerBlockReport("original");
  original.setFailoverAttempts({attempt("a:11211")});

  ReplyCommon copy = original;
  copy.setArtilleryServerBlockReport("copy");
  copy.setFailoverAttempts({});

  EXPECT_EQ(original.artilleryServerBlockReport(), "original");
  ASSERT_EQ(original.failoverAttempts().size(), 1);
  EXPECT_EQ(copy.artilleryServerBlockReport(), "copy");
  EXPECT_TRUE(copy.failoverAttempts().empty());
}

TEST(ReplyCommonTest, ReleaseFailoverAttemptsClearsOnlyTheAttempts) {
  ReplyCommon reply;
  reply.setArtilleryServerBlockReport("report");
  reply.setFailoverAttempts({attempt("a:11211"), attempt("b:11211")});

  auto released = reply.releaseFailoverAttempts();

  ASSERT_EQ(released.size(), 2);
  EXPECT_EQ(released[1].destination, "b:11211");
  EXPECT_TRUE(reply.failoverAttempts().empty());
  EXPECT_EQ(reply.artilleryServerBlockReport(), "report");
}

TEST(ReplyCommonTest, ReleaseOnACopyLeavesTheOriginalUnchanged) {
  ReplyCommon original;
  original.setArtilleryServerBlockReport("report");
  original.setFailoverAttempts({attempt("a:11211")});
  ReplyCommon copy = original;

  auto released = copy.releaseFailoverAttempts();

  ASSERT_EQ(released.size(), 1);
  EXPECT_EQ(released[0].destination, "a:11211");
  EXPECT_TRUE(copy.failoverAttempts().empty());
  EXPECT_EQ(copy.artilleryServerBlockReport(), "report");
  ASSERT_EQ(original.failoverAttempts().size(), 1);
  EXPECT_EQ(original.failoverAttempts()[0].destination, "a:11211");
  EXPECT_EQ(original.artilleryServerBlockReport(), "report");
}

TEST(ReplyCommonTest, ReleaseWithoutAReportLeavesAnEmptyReply) {
  ReplyCommon reply;
  reply.setFailoverAttempts({attempt("a:11211")});

  auto released = reply.releaseFailoverAttempts();

  ASSERT_EQ(released.size(), 1);
  EXPECT_EQ(released[0].destination, "a:11211");
  EXPECT_TRUE(reply.failoverAttempts().empty());
  EXPECT_TRUE(reply.artilleryServerBlockReport().empty());
  EXPECT_TRUE(reply.releaseFailoverAttempts().empty());
}

TEST(ReplyCommonTest, ClearingEveryTracingFieldLeavesThemEmpty) {
  ReplyCommon reply;
  reply.setArtilleryServerBlockReport("report");

  reply.setArtilleryServerBlockReport({});
  reply.setFailoverAttempts({});

  EXPECT_TRUE(reply.artilleryServerBlockReport().empty());
  EXPECT_TRUE(reply.failoverAttempts().empty());
}

} // namespace
} // namespace carbon
