/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <typeindex>

#include <arpa/inet.h>
#include <gtest/gtest.h>

#include <folly/io/IOBuf.h>

#include <mcrouter/lib/McReply.h>
#include <mcrouter/lib/network/McAsciiParser.h>
#include <mcrouter/lib/network/ClientMcParser.h>

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

std::vector<std::vector<size_t>> genAllSplits(size_t length,
                                              size_t maxPieceSize) {
  if (maxPieceSize == 0) {
    maxPieceSize = length;
  }
  static std::unordered_map<std::pair<size_t, size_t>,
                            std::vector<std::vector<size_t>>> prec;

  auto it = prec.find(std::make_pair(length, maxPieceSize));
  if (it != prec.end()) {
    return it->second;
  }

  std::vector<std::vector<size_t>> splits;
  if (length <= maxPieceSize) {
    splits.push_back({length});
  }

  for (auto piece = 1; piece < std::min(maxPieceSize + 1, length); ++piece) {
    auto vecs = genAllSplits(length - piece, maxPieceSize);
    splits.reserve(splits.size() + vecs.size());
    for (auto& v : vecs) {
      v.push_back(piece);
      splits.push_back(std::move(v));
    }
  }

  prec[std::make_pair(length, maxPieceSize)] = splits;

  return splits;
}

class McAsciiParserHarness {
 public:
  explicit McAsciiParserHarness(folly::IOBuf data) : data_(std::move(data)) {}
  explicit McAsciiParserHarness(const char* str)
    : data_(IOBuf::COPY_BUFFER, str, strlen(str)) {}

  template <class Operation, class Request>
  void expectNext(typename ReplyType<Operation, Request>::type reply,
                  bool failure = false);

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

  template <class Operation, class Request>
  class ReplyInfo :
    public ReplyInfoWithReply<typename ReplyType<Operation, Request>::type> {
   public:
    using ReplyT = typename ReplyType<Operation, Request>::type;

    ReplyInfo(ReplyT reply, bool failure)
      : ReplyInfoWithReply<ReplyT>(std::move(reply), failure) {
    }

    virtual void initializeParser(ParserT& parser) const override {
      parser.expectNext<Operation, Request>();
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
    parser_ =
        folly::make_unique<ParserT>(*this, 0, 1024, 4096, mc_ascii_protocol);
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

template <class Operation, class Request>
void McAsciiParserHarness::expectNext(
  typename ReplyType<Operation, Request>::type reply, bool failure) {
  replies_.push_back(folly::make_unique<ReplyInfo<Operation, Request>>(
    std::move(reply), failure));
}

void McAsciiParserHarness::runTest(int maxPieceSize) {
  // Run the test for original data.
  runTestImpl();

  if (maxPieceSize >= 0) {
    auto storedData = std::move(data_);
    storedData.coalesce();
    auto splits = genAllSplits(storedData.length(),
                               static_cast<size_t>(maxPieceSize));
    LOG(INFO) << "Number of tests generated: " << splits.size();
    for (const auto& split : splits) {
      //std::string s = "Running for: "; // for info
      std::unique_ptr<folly::IOBuf> buffer;
      size_t start = 0;
      for (const auto& piece : split) {
        auto p = folly::IOBuf::copyBuffer(storedData.data() + start, piece);
        // s += "'" + folly::cEscape<std::string>(
        //              folly::StringPiece(p->coalesce())) + "', ";
        if (start == 0) {
          buffer = std::move(p);
        } else {
          buffer->prependChain(std::move(p));
        }
        start += piece;
      }
      data_ = std::move(*buffer);
      runTestImpl();
    }
  }
}

/* Methods for quick Reply construction */

template <class Reply>
Reply setFlags(Reply reply, uint64_t flags) {
  reply.setFlags(flags);
  return reply;
}

template <class Reply>
Reply setLeaseToken(Reply reply, uint64_t token) {
  reply.setLeaseToken(token);
  return reply;
}

template <class Reply>
Reply setDelta(Reply reply, uint64_t delta) {
  reply.setDelta(delta);
  return reply;
}

template <class Reply>
Reply setCas(Reply reply, uint64_t cas) {
  reply.setCas(cas);
  return reply;
}

template <class Reply>
Reply setErrCode(Reply reply, uint32_t errCode) {
  reply.setErrCode(errCode);
  return reply;
}

McReply createMetagetHitReply(uint32_t age, uint32_t exptime,
                           uint64_t flags, std::string host) {
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

}  // anonymous

TEST(McAsciiParserHarness, GetHit) {
  McAsciiParserHarness h("VALUE t 10 2\r\nte\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "te"), 10));
  h.runTest(2);
}

TEST(McAsciiParserHarness, GetHit_Empty) {
  McAsciiParserHarness h("VALUE t 5 0\r\n\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, ""), 5));
  h.runTest(2);
}

TEST(McAsciiParserHarness, GetHit_WithSpaces) {
  McAsciiParserHarness h("VALUE  test  15889  5\r\ntest \r\nEND\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "test "), 15889));
  h.runTest(1);
}

TEST(McAsciiParserHarness, GetHit_Error) {
  McAsciiParserHarness h("VALUE  test  15a889  5\r\ntest \r\nEND\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(McReply(), true);
  h.runTest(1);
}

TEST(McAsciiParserHarness, GetMiss) {
  McAsciiParserHarness h("END\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(McReply(mc_res_notfound));
  h.runTest(0);
}

TEST(McAsciiParserHarness, GetMiss_Error) {
  McAsciiParserHarness h("EnD\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(McReply(), true);
  h.runTest(0);
}

TEST(McAsciiParserHarness, GetClientError) {
  McAsciiParserHarness h("CLIENT_ERROR what\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    McReply(mc_res_client_error, "what"));
  h.runTest(3);
}

TEST(McAsciiParserHarness, GetServerError) {
  McAsciiParserHarness h("SERVER_ERROR what\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    McReply(mc_res_remote_error, "what"));
  h.runTest(3);
}

TEST(McAsciiParserHarness, GetHitMiss) {
  McAsciiParserHarness h("VALUE test 17  5\r\ntest \r\nEND\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "test "), 17));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    McReply(mc_res_notfound), false);
  h.runTest(1);
}

TEST(McAsciiParserHarness, GetsHit) {
  McAsciiParserHarness h("VALUE test 1120 10 573\r\ntest test \r\nEND\r\n");
  h.expectNext<McOperation<mc_op_gets>, McRequest>(
    setCas(setFlags(McReply(mc_res_found, "test test "), 1120), 573));
  h.runTest(1);
}

TEST(McAsciiParserHarness, LeaseGetHit) {
  McAsciiParserHarness h("VALUE test 1120 10\r\ntest test \r\nEND\r\n");
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setFlags(McReply(mc_res_found, "test test "), 1120));
  h.runTest(1);
}

TEST(McAsciiParserHarness, LeaseGetFoundStale) {
  McAsciiParserHarness h("LVALUE test 1 1120 10\r\ntest test \r\nEND\r\n");
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setLeaseToken(setFlags(McReply(mc_res_notfound, "test test "), 1120), 1));
  h.runTest(1);
}

TEST(McAsciiParserHarness, LeaseGetHotMiss) {
  McAsciiParserHarness h("LVALUE test 1 1120 0\r\n\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setLeaseToken(setFlags(McReply(mc_res_notfound), 1120), 1));
  h.runTest(1);
}

TEST(McAsciiParserHarness, LeaseGetMiss) {
  McAsciiParserHarness h("LVALUE test 162481237786486239 112 0\r\n\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setLeaseToken(setFlags(McReply(mc_res_notfound), 112),
                  162481237786486239ull));
  h.runTest(1);
}

TEST(McAsciiParserHarness, SetStored) {
  McAsciiParserHarness h("STORED\r\n");
  h.expectNext<McOperation<mc_op_set>, McRequest>(McReply(mc_res_stored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, SetNotStored) {
  McAsciiParserHarness h("NOT_STORED\r\n");
  h.expectNext<McOperation<mc_op_set>, McRequest>(McReply(mc_res_notstored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, AddStored) {
  McAsciiParserHarness h("STORED\r\n");
  h.expectNext<McOperation<mc_op_add>, McRequest>(McReply(mc_res_stored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, AddNotStored) {
  McAsciiParserHarness h("NOT_STORED\r\n");
  h.expectNext<McOperation<mc_op_add>, McRequest>(McReply(mc_res_notstored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, AddExists) {
  McAsciiParserHarness h("EXISTS\r\n");
  h.expectNext<McOperation<mc_op_add>, McRequest>(McReply(mc_res_exists));
  h.runTest(0);
}

TEST(McAsciiParserHarness, LeaseSetStored) {
  McAsciiParserHarness h("STORED\r\n");
  h.expectNext<McOperation<mc_op_lease_set>, McRequest>(McReply(mc_res_stored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, LeaseSetNotStored) {
  McAsciiParserHarness h("NOT_STORED\r\n");
  h.expectNext<McOperation<mc_op_lease_set>, McRequest>(
    McReply(mc_res_notstored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, LeaseSetStaleStored) {
  McAsciiParserHarness h("STALE_STORED\r\n");
  h.expectNext<McOperation<mc_op_lease_set>, McRequest>(
    McReply(mc_res_stalestored));
  h.runTest(0);
}

TEST(McAsciiParserHarness, IncrSuccess) {
  McAsciiParserHarness h("3636\r\n");
  h.expectNext<McOperation<mc_op_incr>, McRequest>(
    setDelta(McReply(mc_res_stored), 3636));
  h.runTest(0);
}

TEST(McAsciiParserHarness, IncrNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<McOperation<mc_op_incr>, McRequest>(McReply(mc_res_notfound));
  h.runTest(0);
}

TEST(McAsciiParserHarness, DecrSuccess) {
  McAsciiParserHarness h("1534\r\n");
  h.expectNext<McOperation<mc_op_decr>, McRequest>(
    setDelta(McReply(mc_res_stored), 1534));
  h.runTest(0);
}

TEST(McAsciiParserHarness, DecrNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<McOperation<mc_op_decr>, McRequest>(McReply(mc_res_notfound));
  h.runTest(0);
}

TEST(McAsciiParserHarness, Version) {
  McAsciiParserHarness h("VERSION HarnessTest\r\n");
  h.expectNext<McOperation<mc_op_version>, McRequest>(
    McReply(mc_res_ok, "HarnessTest"));
  h.runTest(2);
}

TEST(McAsciiParserHarness, DeleteDeleted) {
  McAsciiParserHarness h("DELETED\r\n");
  h.expectNext<McOperation<mc_op_delete>, McRequest>(McReply(mc_res_deleted));
  h.runTest(0);
}

TEST(McAsciiParserHarness, DeleteNotFound) {
  McAsciiParserHarness h("NOT_FOUND\r\n");
  h.expectNext<McOperation<mc_op_delete>, McRequest>(McReply(mc_res_notfound));
  h.runTest(0);
}

TEST(McAsciiParserHarness, MetagetMiss) {
  McAsciiParserHarness h("END\r\n");
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(McReply(mc_res_notfound));
  h.runTest(0);
}

TEST(McAsciiParserHarness, MetagetHit_Ipv6) {
  McAsciiParserHarness h("META test:key age:345644; exptime:35; "
                         "from:2001:dbaf:7654:7578:12:06ef::1; "
                         "is_transient:38\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(
    createMetagetHitReply(345644, 35, 38, "2001:dbaf:7654:7578:12:06ef::1"));
  h.runTest(1);
}

TEST(McAsciiParserHarness, MetagetHit_Ipv4) {
  McAsciiParserHarness h("META test:key age:  345644; exptime:  35; "
                         "from:  23.84.127.32; "
                         "is_transient:  48\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(
    createMetagetHitReply(345644, 35, 48, "23.84.127.32"));
  h.runTest(1);
}

TEST(McAsciiParserHarness, MetagetHit_Unknown) {
  McAsciiParserHarness h("META test:key age:  unknown; exptime:  37; "
                         "from: unknown; "
                         "is_transient:  48\r\nEND\r\n");
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(
    createMetagetHitReply(-1, 37, 48, "unknown"));
  h.runTest(1);
}

TEST(McAsciiParserHarness, FlushAll) {
  McAsciiParserHarness h("OK\r\n");
  h.expectNext<McOperation<mc_op_flushall>, McRequest>(McReply(mc_res_ok));
  h.runTest(0);
}

TEST(McAsciiParserHarness, AllAtOnce) {
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
                         "OK\r\n");
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "te"), 10));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, ""), 5));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "test "), 15889));
  h.expectNext<McOperation<mc_op_get>, McRequest>(McReply(mc_res_notfound));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    McReply(mc_res_client_error, "what"));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    McReply(mc_res_remote_error, "what"));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "test "), 17));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    McReply(mc_res_notfound));
  h.expectNext<McOperation<mc_op_gets>, McRequest>(
    setCas(setFlags(McReply(mc_res_found, "test test "), 1120), 573));
  h.expectNext<McOperation<mc_op_get>, McRequest>(
    setFlags(McReply(mc_res_found, "test test "), 1120));
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setLeaseToken(setFlags(McReply(mc_res_notfound, "test test "), 1120), 1));
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setLeaseToken(setFlags(McReply(mc_res_notfound), 1120), 1));
  h.expectNext<McOperation<mc_op_lease_get>, McRequest>(
    setLeaseToken(setFlags(McReply(mc_res_notfound), 112),
                  162481237786486239ull));
  h.expectNext<McOperation<mc_op_set>, McRequest>(McReply(mc_res_stored));
  h.expectNext<McOperation<mc_op_set>, McRequest>(McReply(mc_res_notstored));
  h.expectNext<McOperation<mc_op_add>, McRequest>(McReply(mc_res_stored));
  h.expectNext<McOperation<mc_op_add>, McRequest>(McReply(mc_res_notstored));
  h.expectNext<McOperation<mc_op_add>, McRequest>(McReply(mc_res_exists));
  h.expectNext<McOperation<mc_op_lease_set>, McRequest>(McReply(mc_res_stored));
  h.expectNext<McOperation<mc_op_lease_set>, McRequest>(
    McReply(mc_res_notstored));
  h.expectNext<McOperation<mc_op_lease_set>, McRequest>(
    McReply(mc_res_stalestored));
  h.expectNext<McOperation<mc_op_incr>, McRequest>(
    setDelta(McReply(mc_res_stored), 3636));
  h.expectNext<McOperation<mc_op_incr>, McRequest>(McReply(mc_res_notfound));
  h.expectNext<McOperation<mc_op_decr>, McRequest>(
    setDelta(McReply(mc_res_stored), 1534));
  h.expectNext<McOperation<mc_op_decr>, McRequest>(McReply(mc_res_notfound));
  h.expectNext<McOperation<mc_op_version>, McRequest>(
    McReply(mc_res_ok, "HarnessTest"));
  h.expectNext<McOperation<mc_op_delete>, McRequest>(McReply(mc_res_deleted));
  h.expectNext<McOperation<mc_op_delete>, McRequest>(McReply(mc_res_notfound));
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(McReply(mc_res_notfound));
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(
    createMetagetHitReply(345644, 35, 38, "2001:dbaf:7654:7578:12:06ef::1"));
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(
    createMetagetHitReply(345644, 35, 48, "23.84.127.32"));
  h.expectNext<McOperation<mc_op_metaget>, McRequest>(
    createMetagetHitReply(-1, 37, 48, "unknown"));
  h.expectNext<McOperation<mc_op_flushall>, McRequest>(McReply(mc_res_ok));
  h.runTest(1);
}
