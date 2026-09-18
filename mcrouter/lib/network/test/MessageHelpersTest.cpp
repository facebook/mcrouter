/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include <folly/IPAddress.h>
#include "mcrouter/lib/network/MessageHelpers.h"

#include "mcrouter/lib/network/gen/MemcacheMessages.h"

using namespace facebook::memcache;

// Compile-time SFINAE checks: the trait must match exactly the two carbon
// types that carry a non-optional `i64 leaseToken` field, and nothing else.
// If a future Memcache.idl change flips the field type (e.g. to optional or
// terse) the static_asserts will break the build instead of silently dropping
// the column from the mcrouter_requests Scuba dataset.
static_assert(HasLeaseTokenTrait<McLeaseGetReply>::value);
static_assert(HasLeaseTokenTrait<McLeaseSetRequest>::value);
static_assert(!HasLeaseTokenTrait<McLeaseGetRequest>::value);
static_assert(!HasLeaseTokenTrait<McLeaseSetReply>::value);
static_assert(!HasLeaseTokenTrait<McGetRequest>::value);
static_assert(!HasLeaseTokenTrait<McGetReply>::value);
static_assert(!HasLeaseTokenTrait<McSetRequest>::value);
static_assert(!HasLeaseTokenTrait<McSetReply>::value);
static_assert(!HasLeaseTokenTrait<McDeleteRequest>::value);

TEST(MessageHelpersTest, GetLeaseTokenIfExist_LeaseGetReply_ReturnsValue) {
  McLeaseGetReply reply;
  reply.leaseToken() = 12345;
  EXPECT_EQ(getLeaseTokenIfExist(reply), std::optional<int64_t>{12345});
}

TEST(MessageHelpersTest, GetLeaseTokenIfExist_LeaseSetRequest_ReturnsValue) {
  McLeaseSetRequest request("key");
  request.leaseToken() = 67890;
  EXPECT_EQ(getLeaseTokenIfExist(request), std::optional<int64_t>{67890});
}

TEST(MessageHelpersTest, GetLeaseTokenIfExist_LeaseTokenZero_ReturnsZero) {
  // Zero is a legitimate token value and must be distinguishable from absence.
  McLeaseGetReply reply;
  reply.leaseToken() = 0;
  EXPECT_EQ(getLeaseTokenIfExist(reply), std::optional<int64_t>{0});
}

TEST(MessageHelpersTest, GetLeaseTokenIfExist_NonLeaseTypes_ReturnsNullopt) {
  McGetRequest getRequest("key");
  McGetReply getReply;
  McSetRequest setRequest("key");
  McSetReply setReply;
  McLeaseGetRequest leaseGetRequest("key");
  McLeaseSetReply leaseSetReply;

  EXPECT_EQ(getLeaseTokenIfExist(getRequest), std::nullopt);
  EXPECT_EQ(getLeaseTokenIfExist(getReply), std::nullopt);
  EXPECT_EQ(getLeaseTokenIfExist(setRequest), std::nullopt);
  EXPECT_EQ(getLeaseTokenIfExist(setReply), std::nullopt);
  EXPECT_EQ(getLeaseTokenIfExist(leaseGetRequest), std::nullopt);
  EXPECT_EQ(getLeaseTokenIfExist(leaseSetReply), std::nullopt);
}

// copyInheritedRequestFields is the single definition of what a derived
// sub-request inherits from the request it was built from. Route handles that
// construct sub-requests from scratch call only this, so a field that stops
// being copied here silently stops being copied everywhere.
TEST(MessageHelpersTest, CopyInheritedRequestFields_CarriesEveryContextField) {
  McSetRequest from("parent_key");
  from.setKcbIdentity("memcache_test_acl");
  from.setCryptoAuthToken(std::string("serialized-cat"));
  from.setClientIdentifier("hashed-tls-identity");
  from.setPrivacyLibAgenticContext(std::string("agentic-context"));
  from.setSourceIpAddr(folly::IPAddress("::1"));
  from.setWriteTimestampNs(1234567890);
  from.setTraceContext("trace-ctx");
  from.setRegionFlag();
  from.mcTenantId() = 4242;
  from.bucketId() = "bucket-7";
  from.productId() = 99;
  from.regionalizationEntity() = 7;

  McGetRequest to("chunk_key");
  copyInheritedRequestFields(from, to);

  EXPECT_EQ(from.getKcbIdentity(), to.getKcbIdentity());
  EXPECT_EQ(from.getCryptoAuthToken(), to.getCryptoAuthToken());
  EXPECT_EQ(from.getClientIdentifier(), to.getClientIdentifier());
  EXPECT_EQ(
      from.getPrivacyLibAgenticContext(), to.getPrivacyLibAgenticContext());
  EXPECT_EQ(from.getSourceIpAddr(), to.getSourceIpAddr());
  EXPECT_EQ(from.getWriteTimestampNs(), to.getWriteTimestampNs());
  EXPECT_EQ(from.traceContext(), to.traceContext());
  EXPECT_EQ(from.hasRegionFlag(), to.hasRegionFlag());
  // has_value() first: dereferencing an unset optional_field_ref throws
  // bad_optional_field_access, which would abort the test instead of
  // reporting which field stopped being copied.
  ASSERT_TRUE(to.mcTenantId().has_value());
  ASSERT_TRUE(to.bucketId().has_value());
  ASSERT_TRUE(to.productId().has_value());
  ASSERT_TRUE(to.regionalizationEntity().has_value());
  EXPECT_EQ(*from.mcTenantId(), *to.mcTenantId());
  EXPECT_EQ(*from.bucketId(), *to.bucketId());
  EXPECT_EQ(*from.productId(), *to.productId());
  EXPECT_EQ(*from.regionalizationEntity(), *to.regionalizationEntity());

  // The key is the sub-request's own and must survive the copy.
  EXPECT_EQ("chunk_key", to.key()->fullKey());
}

// An unset field on the parent must stay unset on the child rather than
// becoming an engaged empty value.
TEST(MessageHelpersTest, CopyInheritedRequestFields_LeavesUnsetFieldsUnset) {
  McSetRequest from("parent_key");
  McGetRequest to("chunk_key");
  copyInheritedRequestFields(from, to);

  EXPECT_FALSE(to.getKcbIdentity().has_value());
  EXPECT_FALSE(to.getCryptoAuthToken().has_value());
  EXPECT_FALSE(to.getClientIdentifier().has_value());
  EXPECT_FALSE(to.getPrivacyLibAgenticContext().has_value());
  EXPECT_FALSE(to.getSourceIpAddr().has_value());
  EXPECT_FALSE(to.getWriteTimestampNs().has_value());
  EXPECT_FALSE(to.mcTenantId().has_value());
  EXPECT_FALSE(to.bucketId().has_value());
  EXPECT_FALSE(to.productId().has_value());
  EXPECT_FALSE(to.regionalizationEntity().has_value());
}
