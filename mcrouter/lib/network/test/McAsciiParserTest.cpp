/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <arpa/inet.h>

#include <typeindex>

#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/ClientMcParser.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/McAsciiParser.h"
#include "mcrouter/lib/network/test/TestMcAsciiParserUtil.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

using namespace facebook::memcache;

using folly::IOBuf;

namespace {

void compare(const McReply& expected, const McReply& actual) {
  EXPECT_EQ(expected.result(), actual.result());
  EXPECT_EQ(expected.valueRangeSlow(), actual.valueRangeSlow());
  EXPECT_EQ(expected.flags(), actual.flags());
  EXPECT_EQ(expected.leaseToken(), actual.leaseToken());
  EXPECT_EQ(expected.delta(), actual.delta());
  EXPECT_EQ(expected.cas(), actual.cas());
  EXPECT_EQ(expected.appSpecificErrorCode(), actual.appSpecificErrorCode());
  EXPECT_EQ(expected.number(), actual.number());
  EXPECT_EQ(expected.exptime(), actual.exptime());
  EXPECT_EQ(expected.ipv(), actual.ipv());
  EXPECT_EQ(0, memcmp(&expected.ipAddress(), &actual.ipAddress(),
                      sizeof(expected.ipAddress())));
}

template <class ThriftType>
void compare(const TypedThriftReply<ThriftType>& expected,
             const TypedThriftReply<ThriftType>& actual) {
  /**
   * Ensure values are coalesced before apache::thrift::StringTraits<IOBuf>
   * is called to compare them.
   */
  expected.valueRangeSlow();
  actual.valueRangeSlow();
  EXPECT_EQ(*expected, *actual);
}

class McAsciiParserHarness {
 public:
  explicit McAsciiParserHarness(folly::IOBuf data) : data_(std::move(data)) {}
  explicit McAsciiParserHarness(const char* str)
    : data_(IOBuf::COPY_BUFFER, str, strlen(str)) {}

  template <class Request>
  void expectNext(ReplyT<Request> reply, bool failure = false);

  void runTest(int maxPieceSize);
 private:
  using ParserT = ClientMcParser<McAsciiParserHarness>;
  friend ParserT;

  class ReplyInfoBase {
   public:
    bool shouldFail{false};
    std::type_index type;

    virtual ~ReplyInfoBase() {}
    virtual void initializeParser(ParserT& parser) const = 0;
   protected:
    ReplyInfoBase(bool shouldFail_, std::type_index type_)
      : shouldFail(shouldFail_), type(type_) {
    }
  };

  template <class Reply>
  class ReplyInfoWithReply : public ReplyInfoBase {
   public:
    Reply reply;

    ReplyInfoWithReply(Reply reply, bool failure)
      : ReplyInfoBase(failure, typeid(Reply)), reply(std::move(reply)) {
    }
  };

  template <class Request>
  class ReplyInfo :
    public ReplyInfoWithReply<ReplyT<Request>> {
   public:
    using Reply = ReplyT<Request>;

    ReplyInfo(Reply reply, bool failure)
      : ReplyInfoWithReply<Reply>(std::move(reply), failure) {
    }

    void initializeParser(ParserT& parser) const override final {
      parser.expectNext<Request>();
    }
  };

  std::unique_ptr<ParserT> parser_;
  std::vector<std::unique_ptr<ReplyInfoBase>> replies_;
  size_t currentId_{0};
  folly::IOBuf data_;
  bool errorState_{false};

  template <class Reply>
  void replyReady(Reply&& reply, uint64_t reqId) {
    EXPECT_TRUE(currentId_ < replies_.size());
    EXPECT_FALSE(replies_[currentId_]->shouldFail);

    auto& info = *replies_[currentId_];
    EXPECT_TRUE(info.type == typeid(Reply));

    auto& expected = reinterpret_cast<ReplyInfoWithReply<Reply>&>(info).reply;
    compare(expected, reply);

    ++currentId_;
  }

  void parseError(mc_res_t result, folly::StringPiece reason) {
    EXPECT_TRUE(currentId_ < replies_.size());
    EXPECT_TRUE(replies_[currentId_]->shouldFail);
    errorState_ = true;
  }

  bool nextReplyAvailable(uint64_t reqId) {
    EXPECT_TRUE(currentId_ < replies_.size());
    replies_[currentId_]->initializeParser(*parser_);
    return true;
  }

  void runTestImpl() {
    currentId_ = 0;
    errorState_ = false;
    parser_ = folly::make_unique<ParserT>(*this, 1024, 4096);
    for (auto range : data_) {
      while (range.size() > 0 && !errorState_) {
        auto buffer = parser_->getReadBuffer();
        auto readLen = std::min(buffer.second, range.size());
        memcpy(buffer.first, range.begin(), readLen);
        parser_->readDataAvailable(readLen);
        range.advance(readLen);
      }
    }
    // Ensure that we're either replied everything, or encountered parse error.
    EXPECT_TRUE(replies_.size() == currentId_ || errorState_);
  }
};

template <class Request>
void McAsciiParserHarness::expectNext(ReplyT<Request> reply, bool failure) {
  replies_.push_back(folly::make_unique<ReplyInfo<Request>>(
    std::move(reply), failure));
}

void McAsciiParserHarness::runTest(int maxPieceSize) {
  // Run the test for original data.
  runTestImpl();

  if (maxPieceSize >= 0) {
    auto storedData = std::move(data_);
    storedData.coalesce();
    auto splits = genChunkedDataSets(storedData.length(),
                                     static_cast<size_t>(maxPieceSize));
    LOG(INFO) << "Number of tests generated: " << splits.size();
    for (const auto& split : splits) {
      data_ = std::move(*chunkData(storedData, split));
      runTestImpl();
    }
  }
}

/* Methods for quick Reply construction */

template <class Reply>
Reply setValue(Reply reply, folly::StringPiece str) {
  reply.setValue(str);
  return reply;
}

McReply setFlags(McReply reply, uint64_t flags) {
  reply.setFlags(flags);
  return reply;
}

template <class Reply>
Reply setFlags(Reply reply, uint64_t flags) {
  reply->set_flags(flags);
  return reply;
}

McReply setLeaseToken(McReply reply, uint64_t token) {
  reply.setLeaseToken(token);
  return reply;
}

template <class Reply>
Reply setLeaseToken(Reply reply, uint64_t token) {
  reply->set_leaseToken(token);
  return reply;
}

McReply setDelta(McReply reply, uint64_t delta) {
  reply.setDelta(delta);
  return reply;
}

template <class Reply>
Reply setDelta(Reply reply, uint64_t delta) {
  reply->set_delta(delta);
  return reply;
}

McReply setCas(McReply reply, uint64_t cas) {
  reply.setCas(cas);
  return reply;
}

template <class Reply>
Reply setCas(Reply reply, uint64_t cas) {
  reply->set_casToken(cas);
  return reply;
}

McReply setVersion(McReply reply, std::string version) {
  return setValue(std::move(reply), version);
}

template <class Reply>
Reply setVersion(Reply reply, std::string version) {
  reply.setValue(version);
  return reply;
}

template <class Request>
ReplyT<Request> createMetagetHitReply(
    uint32_t age, uint32_t exptime, uint64_t flags, std::string host);

template <>
McReply createMetagetHitReply<McRequestWithMcOp<mc_op_metaget>>(
    uint32_t age, uint32_t exptime, uint64_t flags, std::string host) {

  auto msg = createMcMsgRef();
  msg->number = age;
  msg->exptime = exptime;
  msg->flags = flags;

  if (host != "unknown") {
    struct in6_addr addr;
    memset(&addr, 0, sizeof(addr));
    if (strchr(host.data(), ':') != nullptr) {
      EXPECT_TRUE(inet_pton(AF_INET6, host.data(), &addr) > 0);
      msg->ipv = 6;
    } else {
      EXPECT_TRUE(inet_pton(AF_INET, host.data(), &addr) > 0);
      msg->ipv = 4;
    }
    msg->ip_addr = addr;
  }
  auto ret = McReply(mc_res_found, std::move(msg));
  if (host != "unknown") {
    ret.setValue(host);
  }
  return ret;
}

template <>
TypedThriftReply<cpp2::McMetagetReply>
createMetagetHitReply<TypedThriftRequest<cpp2::McMetagetRequest>>(
    uint32_t age, uint32_t exptime, uint64_t flags, std::string host) {

  TypedThriftReply<cpp2::McMetagetReply> msg;
  msg->set_age(age);
  msg->set_exptime(exptime);

  if (host != "unknown") {
    struct in6_addr addr;
    memset(&addr, 0, sizeof(addr));
    if (strchr(host.data(), ':') != nullptr) {
      EXPECT_TRUE(inet_pton(AF_INET6, host.data(), &addr) > 0);
      msg->set_ipv(6);
    } else {
      EXPECT_TRUE(inet_pton(AF_INET, host.data(), &addr) > 0);
      msg->set_ipv(4);
    }
  }
  msg->set_result(mc_res_found);
  if (host != "unknown") {
    msg->set_ipAddress(host);
  }
  return msg;
}

}  // anonymous

/**
 * Test get
 */
template <class Request>
class McAsciiParserTestGet : public ::testing::Test {};
using GetTypes = ::testing::Types<McRequestWithMcOp<mc_op_get>,
                                  TypedThriftRequest<cpp2::McGetRequest>>;
TYPED_TEST_CASE(McAsciiParserTestGet, GetTypes);

TYPED_TEST(McAsciiParserTestGet, GetHit) {
  McAsciiParserHarness h("VALUE t 10 2\r\nte\r\nEND\r\n");
  h.expectNext<TypeParam>(
      setFlags(setValue(ReplyT<TypeParam>(mc_res_found), "te"), 10));
  h.runTest(2);
}

TYPED_TEST(McAsciiParserTestGet, GetHit_Empty) {
  McAsciiParserHarness h("VALUE t 5 0\r\n\r\nEND\r\n");
  h.expectNext<TypeParam>(
    setFlags(setValue(ReplyT<TypeParam>(mc_res_found), ""), 5));
  h.runTest(2);
}

TYPED_TEST(McAsciiParserTestGet, GetHit_WithSpaces) {
  McAsciiParserHarness h("VALUE  test  15889  5\r\ntest \r\nEND\r\n");
  h.expectNext<TypeParam>(
    setFlags(setValue(ReplyT<TypeParam>(mc_res_found), "test "), 15889));
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestGet, GetHit_Error) {
  McAsciiParserHarness h("VALUE  test  15a889  5\r\ntest \r\nEND\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(), true);
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestGet, GetMiss) {
  McAsciiParserHarness h("END\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestGet, GetMiss_Error) {
  McAsciiParserHarness h("EnD\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(), true);
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestGet, GetClientError) {
  McAsciiParserHarness h("CLIENT_ERROR what\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_client_error, "what"));
  h.runTest(3);
}

TYPED_TEST(McAsciiParserTestGet, GetServerError) {
  McAsciiParserHarness h("SERVER_ERROR what\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_remote_error, "what"));
  h.runTest(3);
}

TYPED_TEST(McAsciiParserTestGet, GetHitMiss) {
  McAsciiParserHarness h("VALUE test 17  5\r\ntest \r\nEND\r\nEND\r\n");
  h.expectNext<TypeParam>(
    setFlags(setValue(ReplyT<TypeParam>(mc_res_found), "test "), 17));
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound), false);
  h.runTest(1);
}

/**
 * Test gets
 */
template <class Request>
class McAsciiParserTestGets : public ::testing::Test {};
using GetsTypes = ::testing::Types<McRequestWithMcOp<mc_op_gets>,
                                   TypedThriftRequest<cpp2::McGetsRequest>>;
TYPED_TEST_CASE(McAsciiParserTestGets, GetsTypes);

TYPED_TEST(McAsciiParserTestGets, GetsHit) {
  McAsciiParserHarness h("VALUE test 1120 10 573\r\ntest test \r\nEND\r\n");
  h.expectNext<TypeParam>(
    setCas(
      setFlags(
        setValue(ReplyT<TypeParam>(mc_res_found), "test test "), 1120), 573));
  h.runTest(1);
}

/**
 * Test lease-get
 */
template <class Request>
class McAsciiParserTestLeaseGet : public ::testing::Test {};
using LeaseGetTypes =
  ::testing::Types<McRequestWithMcOp<mc_op_lease_get>,
                   TypedThriftRequest<cpp2::McLeaseGetRequest>>;
TYPED_TEST_CASE(McAsciiParserTestLeaseGet, LeaseGetTypes);

TYPED_TEST(McAsciiParserTestLeaseGet, LeaseGetHit) {
  McAsciiParserHarness h("VALUE test 1120 10\r\ntest test \r\nEND\r\n");
  h.expectNext<TypeParam>(
    setFlags(setValue(ReplyT<TypeParam>(mc_res_found), "test test "), 1120));
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestLeaseGet, LeaseGetFoundStale) {
  McAsciiParserHarness h("LVALUE test 1 1120 10\r\ntest test \r\nEND\r\n");
  h.expectNext<TypeParam>(
    setLeaseToken(
      setFlags(
        setValue(ReplyT<TypeParam>(mc_res_notfound), "test test "), 1120), 1));
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestLeaseGet, LeaseGetHotMiss) {
  McAsciiParserHarness h("LVALUE test 1 1120 0\r\n\r\nEND\r\n");
  h.expectNext<TypeParam>(
    setLeaseToken(
      setFlags(
        setValue(ReplyT<TypeParam>(mc_res_notfound), ""), 1120), 1));
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestLeaseGet, LeaseGetMiss) {
  McAsciiParserHarness h("LVALUE test 162481237786486239 112 0\r\n\r\nEND\r\n");
  h.expectNext<TypeParam>(
    setLeaseToken(
      setFlags(
        setValue(ReplyT<TypeParam>(mc_res_notfound), ""),
        112),
      162481237786486239ull));
  h.runTest(1);
}

/**
 * Test set
 */
template <class Request>
class McAsciiParserTestSet: public ::testing::Test {};
using SetTypes = ::testing::Types<McRequestWithMcOp<mc_op_set>,
                                  TypedThriftRequest<cpp2::McSetRequest>>;
TYPED_TEST_CASE(McAsciiParserTestSet, SetTypes);

TYPED_TEST(McAsciiParserTestSet, SetStored) {
  McAsciiParserHarness h("STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_stored));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestSet, SetNotStored) {
  McAsciiParserHarness h("NOT_STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notstored));
  h.runTest(0);
}

/**
 * Test add
 */
template <class Request>
class McAsciiParserTestAdd: public ::testing::Test {};
using AddTypes = ::testing::Types<McRequestWithMcOp<mc_op_add>,
                                  TypedThriftRequest<cpp2::McAddRequest>>;
TYPED_TEST_CASE(McAsciiParserTestAdd, AddTypes);

TYPED_TEST(McAsciiParserTestAdd, AddStored) {
  McAsciiParserHarness h("STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_stored));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestAdd, AddNotStored) {
  McAsciiParserHarness h("NOT_STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notstored));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestAdd, AddExists) {
  McAsciiParserHarness h("EXISTS\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_exists));
  h.runTest(0);
}

/**
 * Test lease-set
 */
template <class Request>
class McAsciiParserTestLeaseSet : public ::testing::Test {};
using LeaseSetTypes =
  ::testing::Types<McRequestWithMcOp<mc_op_lease_set>,
                   TypedThriftRequest<cpp2::McLeaseSetRequest>>;
TYPED_TEST_CASE(McAsciiParserTestLeaseSet, LeaseSetTypes);

TYPED_TEST(McAsciiParserTestLeaseSet, LeaseSetStored) {
  McAsciiParserHarness h("STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_stored));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestLeaseSet, LeaseSetNotStored) {
  McAsciiParserHarness h("NOT_STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notstored));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestLeaseSet, LeaseSetStaleStored) {
  McAsciiParserHarness h("STALE_STORED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_stalestored));
  h.runTest(0);
}

/**
 * Test incr
 */
template <class Request>
class McAsciiParserTestIncr : public ::testing::Test {};
using IncrTypes = ::testing::Types<McRequestWithMcOp<mc_op_incr>,
                                   TypedThriftRequest<cpp2::McIncrRequest>>;
TYPED_TEST_CASE(McAsciiParserTestIncr, IncrTypes);

TYPED_TEST(McAsciiParserTestIncr, IncrSuccess) {
  McAsciiParserHarness h("3636\r\n");
  h.expectNext<TypeParam>(setDelta(ReplyT<TypeParam>(mc_res_stored), 3636));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestIncr, IncrNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound));
  h.runTest(0);
}

/**
 * Test incr
 */
template <class Request>
class McAsciiParserTestDecr : public ::testing::Test {};
using DecrTypes = ::testing::Types<McRequestWithMcOp<mc_op_decr>,
                                   TypedThriftRequest<cpp2::McDecrRequest>>;
TYPED_TEST_CASE(McAsciiParserTestDecr, DecrTypes);

TYPED_TEST(McAsciiParserTestDecr, DecrSuccess) {
  McAsciiParserHarness h("1534\r\n");
  h.expectNext<TypeParam>(setDelta(ReplyT<TypeParam>(mc_res_stored), 1534));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestDecr, DecrNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound));
  h.runTest(0);
}

/**
 * Test version
 */
template <class Request>
class McAsciiParserTestVersion : public ::testing::Test {};
using VersionTypes =
  ::testing::Types<McRequestWithMcOp<mc_op_version>,
                   TypedThriftRequest<cpp2::McVersionRequest>>;
TYPED_TEST_CASE(McAsciiParserTestVersion, VersionTypes);

TYPED_TEST(McAsciiParserTestVersion, Version) {
  McAsciiParserHarness h("VERSION HarnessTest\r\n");
  h.expectNext<TypeParam>(
      setVersion(ReplyT<TypeParam>(mc_res_ok), "HarnessTest"));
  h.runTest(2);
}

/**
 * Test delete
 */
template <class Request>
class McAsciiParserTestDelete : public ::testing::Test {};
using DeleteTypes = ::testing::Types<McRequestWithMcOp<mc_op_delete>,
                                     TypedThriftRequest<cpp2::McDeleteRequest>>;
TYPED_TEST_CASE(McAsciiParserTestDelete, DeleteTypes);

TYPED_TEST(McAsciiParserTestDelete, DeleteDeleted) {
  McAsciiParserHarness h("DELETED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_deleted));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestDelete, DeleteNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound));
  h.runTest(0);
}

/**
 * Test touch
 */
template <class Request>
class McAsciiParserTestTouch : public ::testing::Test {};
using TouchTypes = ::testing::Types<McRequestWithMcOp<mc_op_touch>,
                                     TypedThriftRequest<cpp2::McTouchRequest>>;
TYPED_TEST_CASE(McAsciiParserTestTouch, TouchTypes);

TYPED_TEST(McAsciiParserTestTouch, TouchTouched) {
  McAsciiParserHarness h("TOUCHED\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_touched));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestTouch, TouchNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound));
  h.runTest(0);
}

/**
 * Test metaget
 */
template <class Request>
class McAsciiParserTestMetaget : public ::testing::Test {};
using MetagetTypes =
  ::testing::Types<McRequestWithMcOp<mc_op_metaget>,
                   TypedThriftRequest<cpp2::McMetagetRequest>>;
TYPED_TEST_CASE(McAsciiParserTestMetaget, MetagetTypes);

TYPED_TEST(McAsciiParserTestMetaget, MetagetMiss) {
  McAsciiParserHarness h("END\r\n");
  h.expectNext<TypeParam>(ReplyT<TypeParam>(mc_res_notfound));
  h.runTest(0);
}

TYPED_TEST(McAsciiParserTestMetaget, MetagetHit_Ipv6) {
  McAsciiParserHarness h("META test:key age:345644; exptime:35; "
                         "from:2001:dbaf:7654:7578:12:06ef::1; "
                         "is_transient:38\r\nEND\r\n");
  h.expectNext<TypeParam>(
    createMetagetHitReply<TypeParam>(
      345644, 35, 38, "2001:dbaf:7654:7578:12:06ef::1"));
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestMetaget, MetagetHit_Ipv4) {
  McAsciiParserHarness h("META test:key age:  345644; exptime:  35; "
                         "from:  23.84.127.32; "
                         "is_transient:  48\r\nEND\r\n");
  h.expectNext<TypeParam>(
    createMetagetHitReply<TypeParam>(345644, 35, 48, "23.84.127.32"));
  h.runTest(1);
}

TYPED_TEST(McAsciiParserTestMetaget, MetagetHit_Unknown) {
  McAsciiParserHarness h("META test:key age:  unknown; exptime:  37; "
                         "from: unknown; "
                         "is_transient:  48\r\nEND\r\n");
  h.expectNext<TypeParam>(
    createMetagetHitReply<TypeParam>(-1, 37, 48, "unknown"));
  h.runTest(1);
}

/**
 * Test flush_all
 */
TEST(McAsciiParserTestFlushAll, FlushAll) {
  McAsciiParserHarness h("OK\r\n");
  h.expectNext<McRequestWithMcOp<mc_op_flushall>>(
      McReply(mc_res_ok));
  h.runTest(0);
}

/* TODO(jmswen) Add McFlushAllRequest test */

TEST(McAsciiParserTestAll, AllAtOnce) {
  /**
   * Parse all non-failure tests as one stream.
   */
  McAsciiParserHarness h("VALUE t 10 2\r\nte\r\nEND\r\n"
                         "VALUE t 5 0\r\n\r\nEND\r\n"
                         "VALUE  test  15889  5\r\ntest \r\nEND\r\n"
                         "END\r\n"
                         "CLIENT_ERROR what\r\n"
                         "SERVER_ERROR what\r\n"
                         "VALUE test 17  5\r\ntest \r\nEND\r\nEND\r\n"
                         "VALUE test 1120 10 573\r\ntest test \r\nEND\r\n"
                         "VALUE test 1120 10\r\ntest test \r\nEND\r\n"
                         "LVALUE test 1 1120 10\r\ntest test \r\nEND\r\n"
                         "LVALUE test 1 1120 0\r\n\r\nEND\r\n"
                         "LVALUE test 162481237786486239 112 0\r\n\r\nEND\r\n"
                         "STORED\r\n"
                         "NOT_STORED\r\n"
                         "STORED\r\n"
                         "NOT_STORED\r\n"
                         "EXISTS\r\n"
                         "STORED\r\n"
                         "NOT_STORED\r\n"
                         "STALE_STORED\r\n"
                         "3636\r\n"
                         "NOT_FOUND\r\n"
                         "1534\r\n"
                         "NOT_FOUND\r\n"
                         "VERSION HarnessTest\r\n"
                         "DELETED\r\n"
                         "NOT_FOUND\r\n"
                         "END\r\n"
                         "META test:key age:345644; exptime:35; "
                         "from:2001:dbaf:7654:7578:12:06ef::1; "
                         "is_transient:38\r\nEND\r\n"
                         "META test:key age:  345644; exptime:  35; "
                         "from:  23.84.127.32; "
                         "is_transient:  48\r\nEND\r\n"
                         "META test:key age:  unknown; exptime:  37; "
                         "from: unknown; "
                         "is_transient:  48\r\nEND\r\n"
                         "OK\r\n"
                         "TOUCHED\r\n");
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    setFlags(McReply(mc_res_found, "te"), 10));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    setFlags(McReply(mc_res_found, ""), 5));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    setFlags(McReply(mc_res_found, "test "), 15889));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
      McReply(mc_res_notfound));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    McReply(mc_res_client_error, "what"));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    McReply(mc_res_remote_error, "what"));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    setFlags(McReply(mc_res_found, "test "), 17));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    McReply(mc_res_notfound));
  h.expectNext<McRequestWithMcOp<mc_op_gets>>(
    setCas(setFlags(McReply(mc_res_found, "test test "), 1120), 573));
  h.expectNext<McRequestWithMcOp<mc_op_get>>(
    setFlags(McReply(mc_res_found, "test test "), 1120));
  h.expectNext<McRequestWithMcOp<mc_op_lease_get>>(
    setLeaseToken(setFlags(McReply(mc_res_notfound, "test test "), 1120), 1));
  h.expectNext<McRequestWithMcOp<mc_op_lease_get>>(
    setLeaseToken(setFlags(McReply(mc_res_notfound), 1120), 1));
  h.expectNext<McRequestWithMcOp<mc_op_lease_get>>(
    setLeaseToken(setFlags(McReply(mc_res_notfound), 112),
                  162481237786486239ull));
  h.expectNext<McRequestWithMcOp<mc_op_set>>(McReply(mc_res_stored));
  h.expectNext<McRequestWithMcOp<mc_op_set>>(
      McReply(mc_res_notstored));
  h.expectNext<McRequestWithMcOp<mc_op_add>>(McReply(mc_res_stored));
  h.expectNext<McRequestWithMcOp<mc_op_add>>(
      McReply(mc_res_notstored));
  h.expectNext<McRequestWithMcOp<mc_op_add>>(McReply(mc_res_exists));
  h.expectNext<McRequestWithMcOp<mc_op_lease_set>>(
      McReply(mc_res_stored));
  h.expectNext<McRequestWithMcOp<mc_op_lease_set>>(
    McReply(mc_res_notstored));
  h.expectNext<McRequestWithMcOp<mc_op_lease_set>>(
    McReply(mc_res_stalestored));
  h.expectNext<McRequestWithMcOp<mc_op_incr>>(
    setDelta(McReply(mc_res_stored), 3636));
  h.expectNext<McRequestWithMcOp<mc_op_incr>>(
      McReply(mc_res_notfound));
  h.expectNext<McRequestWithMcOp<mc_op_decr>>(
    setDelta(McReply(mc_res_stored), 1534));
  h.expectNext<McRequestWithMcOp<mc_op_decr>>(
      McReply(mc_res_notfound));
  h.expectNext<McRequestWithMcOp<mc_op_version>>(
    McReply(mc_res_ok, "HarnessTest"));
  h.expectNext<McRequestWithMcOp<mc_op_delete>>(
      McReply(mc_res_deleted));
  h.expectNext<McRequestWithMcOp<mc_op_delete>>(
      McReply(mc_res_notfound));
  h.expectNext<McRequestWithMcOp<mc_op_metaget>>(
      McReply(mc_res_notfound));
  h.expectNext<McRequestWithMcOp<mc_op_metaget>>(
      createMetagetHitReply<McRequestWithMcOp<mc_op_metaget>>(
        345644, 35, 38, "2001:dbaf:7654:7578:12:06ef::1"));
  h.expectNext<McRequestWithMcOp<mc_op_metaget>>(
      createMetagetHitReply<McRequestWithMcOp<mc_op_metaget>>(
        345644, 35, 48, "23.84.127.32"));
  h.expectNext<McRequestWithMcOp<mc_op_metaget>>(
      createMetagetHitReply<McRequestWithMcOp<mc_op_metaget>>(
        -1, 37, 48, "unknown"));
  h.expectNext<McRequestWithMcOp<mc_op_flushall>>(
      McReply(mc_res_ok));
  h.expectNext<McRequestWithMcOp<mc_op_touch>>(
      McReply(mc_res_touched));
  h.runTest(1);
}

TEST(McAsciiParserHarness, AllAtOnceTyped) {
  /**
   *    * Parse all non-failure tests as one stream.
   *       */
  McAsciiParserHarness h("VALUE t 10 2\r\nte\r\nEND\r\n"
                         "VALUE t 5 0\r\n\r\nEND\r\n"
                         "VALUE  test  15889  5\r\ntest \r\nEND\r\n"
                         "END\r\n"
                         "CLIENT_ERROR what\r\n"
                         "SERVER_ERROR what\r\n"
                         "VALUE test 17  5\r\ntest \r\nEND\r\nEND\r\n"
                         "VALUE test 1120 10 573\r\ntest test \r\nEND\r\n"
                         "VALUE test 1120 10\r\ntest test \r\nEND\r\n"
                         "LVALUE test 1 1120 10\r\ntest test \r\nEND\r\n"
                         "LVALUE test 1 1120 0\r\n\r\nEND\r\n"
                         "LVALUE test 162481237786486239 112 0\r\n\r\nEND\r\n"
                         "STORED\r\n"
                         "NOT_STORED\r\n"
                         "STORED\r\n"
                         "NOT_STORED\r\n"
                         "EXISTS\r\n"
                         "STORED\r\n"
                         "NOT_STORED\r\n"
                         "STALE_STORED\r\n"
                         "3636\r\n"
                         "NOT_FOUND\r\n"
                         "1534\r\n"
                         "NOT_FOUND\r\n"
                         "VERSION HarnessTest\r\n"
                         "DELETED\r\n"
                         "NOT_FOUND\r\n"
                         "END\r\n"
                         "META test:key age:345644; exptime:35; "
                         "from:2001:dbaf:7654:7578:12:06ef::1; "
                         "is_transient:38\r\nEND\r\n"
                         "META test:key age:  345644; exptime:  35; "
                         "from:  23.84.127.32; "
                         "is_transient:  48\r\nEND\r\n"
                         "META test:key age:  unknown; exptime:  37; "
                         "from: unknown; "
                         "is_transient:  48\r\nEND\r\n"
                         "TOUCHED\r\n");
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      setFlags(setValue(TypedThriftReply<cpp2::McGetReply>(mc_res_found), "te"),
               10));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      setFlags(setValue(TypedThriftReply<cpp2::McGetReply>(mc_res_found), ""),
               5));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      setFlags(
        setValue(TypedThriftReply<cpp2::McGetReply>(mc_res_found), "test "),
        15889));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      TypedThriftReply<cpp2::McGetReply>(mc_res_notfound));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      TypedThriftReply<cpp2::McGetReply>(mc_res_client_error, "what"));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      TypedThriftReply<cpp2::McGetReply>(mc_res_remote_error, "what"));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      setFlags(
        setValue(TypedThriftReply<cpp2::McGetReply>(mc_res_found), "test "),
        17));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      TypedThriftReply<cpp2::McGetReply>(mc_res_notfound));
  h.expectNext<TypedThriftRequest<cpp2::McGetsRequest>>(
      setCas(
        setFlags(
          setValue(TypedThriftReply<cpp2::McGetsReply>(mc_res_found),
            "test test "),
          1120),
        573));
  h.expectNext<TypedThriftRequest<cpp2::McGetRequest>>(
      setFlags(
        setValue(
          TypedThriftReply<cpp2::McGetReply>(mc_res_found), "test test "),
        1120));
  h.expectNext<TypedThriftRequest<cpp2::McLeaseGetRequest>>(
      setLeaseToken(
        setFlags(
          setValue(
            TypedThriftReply<cpp2::McLeaseGetReply>(mc_res_notfound),
            "test test "),
          1120),
        1));
  h.expectNext<TypedThriftRequest<cpp2::McLeaseGetRequest>>(
      setLeaseToken(
        setFlags(
          setValue(
            TypedThriftReply<cpp2::McLeaseGetReply>(mc_res_notfound), ""),
          1120),
        1));
  h.expectNext<TypedThriftRequest<cpp2::McLeaseGetRequest>>(
      setLeaseToken(
        setFlags(
          setValue(
            TypedThriftReply<cpp2::McLeaseGetReply>(mc_res_notfound), ""),
          112),
        162481237786486239ull));
  h.expectNext<TypedThriftRequest<cpp2::McSetRequest>>(
      TypedThriftReply<cpp2::McSetReply>(mc_res_stored));
  h.expectNext<TypedThriftRequest<cpp2::McSetRequest>>(
      TypedThriftReply<cpp2::McSetReply>(mc_res_notstored));
  h.expectNext<TypedThriftRequest<cpp2::McAddRequest>>(
      TypedThriftReply<cpp2::McAddReply>(mc_res_stored));
  h.expectNext<TypedThriftRequest<cpp2::McAddRequest>>(
      TypedThriftReply<cpp2::McAddReply>(mc_res_notstored));
  h.expectNext<TypedThriftRequest<cpp2::McAddRequest>>(
      TypedThriftReply<cpp2::McAddReply>(mc_res_exists));
  h.expectNext<TypedThriftRequest<cpp2::McLeaseSetRequest>>(
      TypedThriftReply<cpp2::McLeaseSetReply>(mc_res_stored));
  h.expectNext<TypedThriftRequest<cpp2::McLeaseSetRequest>>(
      TypedThriftReply<cpp2::McLeaseSetReply>(mc_res_notstored));
  h.expectNext<TypedThriftRequest<cpp2::McLeaseSetRequest>>(
      TypedThriftReply<cpp2::McLeaseSetReply>(mc_res_stalestored));
  h.expectNext<TypedThriftRequest<cpp2::McIncrRequest>>(
      setDelta(TypedThriftReply<cpp2::McIncrReply>(mc_res_stored), 3636));
  h.expectNext<TypedThriftRequest<cpp2::McIncrRequest>>(
      TypedThriftReply<cpp2::McIncrReply>(mc_res_notfound));
  h.expectNext<TypedThriftRequest<cpp2::McDecrRequest>>(
      setDelta(TypedThriftReply<cpp2::McDecrReply>(mc_res_stored), 1534));
  h.expectNext<TypedThriftRequest<cpp2::McDecrRequest>>(
      TypedThriftReply<cpp2::McDecrReply>(mc_res_notfound));
  h.expectNext<TypedThriftRequest<cpp2::McVersionRequest>>(
      setVersion(
        TypedThriftReply<cpp2::McVersionReply>(mc_res_ok), "HarnessTest"));
  h.expectNext<TypedThriftRequest<cpp2::McDeleteRequest>>(
      TypedThriftReply<cpp2::McDeleteReply>(mc_res_deleted));
  h.expectNext<TypedThriftRequest<cpp2::McDeleteRequest>>(
      TypedThriftReply<cpp2::McDeleteReply>(mc_res_notfound));
  h.expectNext<TypedThriftRequest<cpp2::McMetagetRequest>>(
      TypedThriftReply<cpp2::McMetagetReply>(mc_res_notfound));
  h.expectNext<TypedThriftRequest<cpp2::McMetagetRequest>>(
      createMetagetHitReply<TypedThriftRequest<cpp2::McMetagetRequest>>(
        345644, 35, 38, "2001:dbaf:7654:7578:12:06ef::1"));
  h.expectNext<TypedThriftRequest<cpp2::McMetagetRequest>>(
      createMetagetHitReply<TypedThriftRequest<cpp2::McMetagetRequest>>(
        345644, 35, 48, "23.84.127.32"));
  h.expectNext<TypedThriftRequest<cpp2::McMetagetRequest>>(
      createMetagetHitReply<TypedThriftRequest<cpp2::McMetagetRequest>>(
        -1, 37, 48, "unknown"));
  h.expectNext<TypedThriftRequest<cpp2::McTouchRequest>>(
      TypedThriftReply<cpp2::McTouchReply>(mc_res_touched));
  h.runTest(1);
}
