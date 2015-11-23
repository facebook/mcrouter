/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include <mcrouter/lib/network/ServerMcParser.h>
#include <mcrouter/lib/network/test/TestMcAsciiParserUtil.h>

using namespace facebook::memcache;

namespace {

template <class Request>
bool compareRequests(const Request& expected, const Request& actual) {
  EXPECT_EQ(expected.fullKey(), actual.fullKey());
  EXPECT_EQ(expected.flags(), actual.flags());
  EXPECT_EQ(expected.exptime(), actual.exptime());
  EXPECT_EQ(expected.cas(), actual.cas());
  EXPECT_EQ(expected.leaseToken(), actual.leaseToken());
  EXPECT_EQ(expected.number(), actual.number());
  EXPECT_EQ(expected.delta(), actual.delta());
  EXPECT_EQ(expected.valueRangeSlow(), actual.valueRangeSlow());

  return expected.fullKey() == actual.fullKey() &&
         expected.flags() == actual.flags() &&
         expected.exptime() == actual.exptime() &&
         expected.cas() == actual.cas() &&
         expected.leaseToken() == actual.leaseToken() &&
         expected.number() == actual.number() &&
         expected.delta() == actual.delta() &&
         expected.valueRangeSlow() == actual.valueRangeSlow();
}

class TestRunner {
 public:
  TestRunner() {}

  TestRunner(const TestRunner&) = delete;
  TestRunner operator=(const TestRunner&) = delete;

  template <class Operation, class Request>
  TestRunner& expectNext(Operation, Request req, bool noreply = false) {
    callbacks_.emplace_back(
        folly::make_unique<ExpectedRequestCallback<Operation, Request>>(
            Operation(), std::move(req), noreply));
    return *this;
  }

  TestRunner& expectMultiOpEnd() {
    callbacks_.emplace_back(folly::make_unique<ExpectedMultiOpEndCallback>());
    return *this;
  }

  TestRunner& expectError() {
    isError_ = true;
    return *this;
  }

  /**
   * Generates multiple possible splits of data and then runs test for each of
   * them.
   */
  TestRunner& run(folly::IOBuf data) {
    data.coalesce();

    // Limit max piece size, since it may produce huge amount of combinations.
    size_t pieceSize = 0;
    size_t cnt = 0;
    while (pieceSize <= data.length() && cnt < 20000) {
      cnt = chunkedDataSetsCnt(data.length(), ++pieceSize);
    }
    auto splits = genChunkedDataSets(data.length(), pieceSize - 1);
    splits.push_back({data.length()});
    for (const auto& split : splits) {
      auto tmp = chunkData(data, split);
      // If we failed a test already, just break.
      if (!runImpl(std::move(*tmp))) {
        return *this;
      }
    }

    return *this;
  }

  template <typename... Args>
  TestRunner& run(folly::StringPiece format, Args... args) {
    return run(folly::sformat(format, std::forward<Args>(args)...));
  }

  TestRunner& run(folly::StringPiece data) {
    run(folly::IOBuf(folly::IOBuf::COPY_BUFFER, data.begin(), data.size()));
    return *this;
  }

 private:
  template <class Operation, class Request> class ExpectedRequestCallback;

  class ExpectedCallbackBase {
   public:
    template <class Operation, class Request>
    bool validate(Operation, const Request& req, bool noreply) const {
      EXPECT_TRUE(opType_ == typeid(Operation))
          << "Parsed wrong op type, expected " << opName_ << ", but parsed "
          << Operation::name;
      EXPECT_TRUE(reqType_ == typeid(Request))
          << "Parsed wrong type of request!";
      EXPECT_EQ(noreply_, noreply);

      if (opType_ == typeid(Operation) && reqType_ == typeid(Request) &&
          noreply == noreply_) {
        auto& message =
            *reinterpret_cast<
                const ExpectedRequestCallback<Operation, Request>*>(this);
        return compareRequests(message.req_, req);
      } else {
        return false;
      }

      return true;
    }

    virtual ~ExpectedCallbackBase() = default;
   protected:
    template <class Operation, class Request>
    ExpectedCallbackBase(Operation, const Request& r, bool noreply)
        : opName_(Operation::name),
          opType_(typeid(Operation)),
          reqType_(typeid(Request)),
          noreply_(noreply) {}

   private:
    const char* opName_;
    std::type_index opType_;
    std::type_index reqType_;
    bool noreply_{false};
  };

  template <class Operation, class Request>
  class ExpectedRequestCallback : public ExpectedCallbackBase {
   public:
    ExpectedRequestCallback(Operation, Request req, bool noreply = false)
        : ExpectedCallbackBase(Operation(), req, noreply),
          req_(std::move(req)) {}
    virtual ~ExpectedRequestCallback() = default;
   private:
    Request req_;

    friend class ExpectedCallbackBase;
  };

  class ExpectedMultiOpEndCallback
      : public ExpectedRequestCallback<McOperation<mc_op_end>, McRequest> {
   public:
    ExpectedMultiOpEndCallback()
        : ExpectedRequestCallback(McOperation<mc_op_end>(), McRequest()) {}
  };

  class ParserOnRequest {
   public:
    ParserOnRequest(std::vector<std::unique_ptr<ExpectedCallbackBase>>& cbs,
                    bool isError) : callbacks_(cbs), isError_(isError) {}

    bool isFinished() const {
      return finished_;
    }

    bool isFailed() const {
      return failed_;
    }

    void setParser(ServerMcParser<ParserOnRequest>* parser) {
      parser_ = parser;
    }
   private:
    std::vector<std::unique_ptr<ExpectedCallbackBase>>& callbacks_;
    ServerMcParser<ParserOnRequest>* parser_{nullptr};
    bool isError_;
    size_t id_{0};
    bool finished_{false};
    bool failed_{false};

    // ServerMcParser callbacks.
    void requestReady(McRequest, mc_op_t, uint32_t, mc_res_t, bool) {
      ASSERT_TRUE(false) << "requestReady should never be called for ASCII";
    }

    void typedRequestReady(uint32_t, const folly::IOBuf&, uint64_t) {
      ASSERT_TRUE(false)
          << "typedRequestReady should never be called for ASCII";
    }

    void parseError(mc_res_t, folly::StringPiece reason) {
      ASSERT_NE(nullptr, parser_)
          << "Test framework bug, didn't provide parser to callback!";
      EXPECT_TRUE(isError_) << "Unexpected parsing error: " << reason
                            << ". Ascii parser message: "
                            << parser_->getUnderlyingAsciiParserError();
      EXPECT_EQ(callbacks_.size(), id_)
          << "Didn't consume all requests, but already failed to parse!";
      finished_ = true;
      failed_ = !isError_ || callbacks_.size() != id_;
    }

    template <class Operation, class Request>
    void checkNext(Operation, Request&& req, bool noreply) {
      EXPECT_LT(id_, callbacks_.size()) << "Unexpected callback!";
      if (id_ < callbacks_.size()) {
        bool validationRes =
            callbacks_[id_]->validate(Operation(), req, noreply);
        EXPECT_TRUE(validationRes)
          << "Wrong callback was called or parsed incorrect request!";
        if (!validationRes) {
          finished_ = true;
          failed_ = true;
          return;
        }

        ++id_;
        if (id_ == callbacks_.size()) {
          // Mark test as finished if we don't expect an error, otherwise we
          // still need to see more data.
          finished_ = !isError_;
        }
      } else {
        finished_ = true;
        failed_ = true;
      }
    }

    // Ascii parser callbacks.
    template <class Operation, class Request>
    void onRequest(Operation, Request&& req, bool noreply) {
      checkNext(Operation(), std::move(req), noreply);
    }

    void multiOpEnd() {
      checkNext(McOperation<mc_op_end>(), McRequest(), false);
    }

    friend class ServerMcParser<ParserOnRequest>;
  };

  std::vector<std::unique_ptr<ExpectedCallbackBase>> callbacks_;
  bool isError_{false};

  bool runImpl(folly::IOBuf data) {
    ParserOnRequest onRequest(callbacks_, isError_);
    ServerMcParser<ParserOnRequest> parser(onRequest,
                                           1024 /* requests per read */,
                                           4096 /* min buffer size */,
                                           4096 /* max buffer size */);
    onRequest.setParser(&parser);

    for (auto range : data) {
      while (range.size() > 0 && !onRequest.isFinished()) {
        auto buffer = parser.getReadBuffer();
        auto readLen = std::min(buffer.second, range.size());
        memcpy(buffer.first, range.begin(), readLen);
        parser.readDataAvailable(readLen);
        range.advance(readLen);
      }
    }

    EXPECT_TRUE(onRequest.isFinished()) << "Not all of the callbacks were "
                                           "called or we didn't encounter "
                                           "error if it was expected!";
    if (!onRequest.isFinished() || onRequest.isFailed()) {
      std::string info = "";
      for (auto range : data) {
        if (!info.empty()) {
          info += ", ";
        }
        info += folly::sformat(
            "\"{}\"", folly::cEscape<std::string>(folly::StringPiece(range)));
      }
      LOG(INFO) << "Test data for failed test: " << info;
      return false;
    }

    return true;
  }
};

std::string createBigValue() {
  const size_t kSize = 16384;
  char bigValue[kSize];
  for (size_t i = 0; i < kSize; ++i) {
    bigValue[i] = (unsigned char)(i % 256);
  }
  return std::string(bigValue, bigValue + kSize);
}

McRequest createSetRequest(folly::StringPiece key,
                           folly::StringPiece value,
                           uint64_t flags,
                           int32_t exptime) {
  // Test regular request
  McRequest r(key);
  r.setValue(
      folly::IOBuf(folly::IOBuf::COPY_BUFFER, value.begin(), value.size()));
  r.setFlags(flags);
  r.setExptime(exptime);
  return r;
}

McRequest createArithmRequest(folly::StringPiece key, double delta) {
  McRequest r(key);
  r.setDelta(delta);
  return r;
}

// Test body for get, gets, lease-get, metaget.
template <class Operation>
void getLikeTest(Operation, std::string opCmd) {
  // Test single key requests.
  TestRunner()
      .expectNext(Operation(), McRequest("test:stepan:1"))
      .expectMultiOpEnd()
      .run(opCmd + " test:stepan:1\r\n")
      .run(opCmd + "   test:stepan:1\r\n")
      .run(opCmd + "   test:stepan:1\n")
      .run(opCmd + " test:stepan:1  \r\n")
      .run(opCmd + " test:stepan:1  \n")
      .expectNext(Operation(), McRequest("test:stepan:2"))
      .expectMultiOpEnd()
      .run(opCmd + " test:stepan:1\r\n" + opCmd + " test:stepan:2\r\n");

  // Test multi key.
  TestRunner()
      .expectNext(Operation(), McRequest("test:stepan:1"))
      .expectNext(Operation(), McRequest("test:stepan:2"))
      .expectMultiOpEnd()
      .run(opCmd + " test:stepan:1 test:stepan:2\r\n")
      .run(opCmd + " test:stepan:1   test:stepan:2\r\n")
      .run(opCmd + " test:stepan:1 test:stepan:2 \r\n")
      .run(opCmd + " test:stepan:1 test:stepan:2 \n")
      .run(opCmd + " test:stepan:1 test:stepan:2\n")
      .expectNext(Operation(), McRequest("test:stepan:3"))
      .expectMultiOpEnd()
      .run(opCmd + " test:stepan:1 test:stepan:2\r\n" +
           opCmd + " test:stepan:3\r\n");

  TestRunner().expectError().run(opCmd + "no:space:before:key\r\n");

  // Missing key.
  TestRunner().expectError().run(opCmd + "\r\n").run(opCmd + "   \r\n");
}

const char* kTestValue = "someSmallTestValue";

// Test body for set, add, replace, append, prepend.
template <class Operation>
void setLikeTest(Operation, std::string opCmd) {
  TestRunner()
      .expectNext(Operation(),
                  createSetRequest("test:stepan:1", kTestValue, 123, 651342))
      .run("{} test:stepan:1 123 651342 {}\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{}   test:stepan:1 123 651342 {}\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1   123 651342 {}\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123   651342 {}\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123 651342 {}  \r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123 651342 {}  \n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123 651342 {}  \r\n{}\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123 651342 {}  \n{}\n",
           opCmd,
           strlen(kTestValue),
           kTestValue);

  // Test negative exptime.
  TestRunner()
      .expectNext(Operation(),
                  createSetRequest("test:stepan:1", kTestValue, 123, -12341232))
      .run("{} test:stepan:1 123 -12341232 {}\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue);

  // Test bad set command format.
  TestRunner().expectError().run(
      "{} test:stepan:1 12as3 -12341232 {}\r\n{}\r\n",
      opCmd,
      strlen(kTestValue),
      kTestValue);

  // Test noreply.
  TestRunner()
      .expectNext(Operation(),
                  createSetRequest("test:stepan:1", kTestValue, 123, 651342),
                  true)
      .run("{} test:stepan:1 123 651342 {} noreply\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123 651342 {}   noreply\r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue)
      .run("{} test:stepan:1 123 651342 {} noreply  \r\n{}\r\n",
           opCmd,
           strlen(kTestValue),
           kTestValue);

  // Test big value.
  std::string bigValue = createBigValue();
  TestRunner()
      .expectNext(Operation(),
                  createSetRequest("test:stepan:1", bigValue, 345, -2))
      .run("{} test:stepan:1 345 -2 {}\r\n{}\r\n",
           opCmd,
           bigValue.size(),
           bigValue);
}

template <class Operation>
void multiTokenOpTest(Operation, std::string opCmd) {
  // With parameter.
  TestRunner()
      .expectNext(Operation(), McRequest("token1"))
      .run(opCmd + " token1\r\n")
      .run(opCmd + "  token1  \r\n");

  // With multiple parameters.
  TestRunner()
      .expectNext(Operation(), McRequest("token1 token2"))
      .run(opCmd + " token1 token2\r\n")
      .run(opCmd + "  token1 token2  \r\n");

  TestRunner()
      .expectNext(Operation(), McRequest("token1   token2"))
      .run(opCmd + " token1   token2\r\n")
      .run(opCmd + "  token1   token2  \r\n");
}

template <class Operation>
void arithmeticTest(Operation, std::string opCmd) {
  TestRunner()
    .expectNext(Operation(), createArithmRequest("test:stepan:1", 1324123))
    .run(opCmd + " test:stepan:1 1324123\r\n")
    .run(opCmd + "     test:stepan:1    1324123   \r\n")
    .run(opCmd + " test:stepan:1  1324123 \r\n");

  TestRunner()
    .expectNext(Operation(), createArithmRequest("test:stepan:1", 1324),
                true)
    .run(opCmd + " test:stepan:1 1324 noreply\r\n")
    .run(opCmd + "     test:stepan:1    1324   noreply  \r\n")
    .run(opCmd + " test:stepan:1  1324 noreply \r\n");

  // No delta.
  TestRunner().expectError()
    .run(opCmd + " test:stepan:1 noreply\r\n")
    .run(opCmd + "     test:stepan:1       noreply  \r\n")
    .run(opCmd + " test:stepan:1   noreply \r\n");
}

}  // anonymous

TEST(McServerAsciiParserHarness, get) {
  getLikeTest(McOperation<mc_op_get>(), "get");
}

TEST(McServerAsciiParserHarness, gets) {
  getLikeTest(McOperation<mc_op_gets>(), "gets");
}

TEST(McServerAsciiParserHarness, lease_get) {
  getLikeTest(McOperation<mc_op_lease_get>(), "lease-get");
}

TEST(McServerAsciiParserHarness, metaget) {
  getLikeTest(McOperation<mc_op_metaget>(), "metaget");
}

TEST(McServerAsciiParserHarness, set) {
  setLikeTest(McOperation<mc_op_set>(), "set");
}

TEST(McServerAsciiParserHarness, add) {
  setLikeTest(McOperation<mc_op_add>(), "add");
}

TEST(McServerAsciiParserHarness, replace) {
  setLikeTest(McOperation<mc_op_replace>(), "replace");
}

TEST(McServerAsciiParserHarness, append) {
  setLikeTest(McOperation<mc_op_append>(), "append");
}

TEST(McServerAsciiParserHarness, prepend) {
  setLikeTest(McOperation<mc_op_prepend>(), "prepend");
}

TEST(McServerAsciiParserHarness, quit) {
  TestRunner()
      .expectNext(McOperation<mc_op_quit>(), McRequest(),
                  true /* quit is always noreply */)
      .run("quit\r\n")
      .run("quit    \r\n");
}

TEST(McServerAsciiParserHarness, version) {
  TestRunner()
      .expectNext(McOperation<mc_op_version>(), McRequest())
      .run("version\r\n")
      .run("version    \r\n");
}

TEST(McServerAsciiParserHarness, shutdown) {
  TestRunner()
      .expectNext(McOperation<mc_op_shutdown>(), McRequest())
      .run("shutdown\r\n")
      .run("shutdown    \r\n");

  // With delay.
  McRequest r;
  r.setNumber(10);
  TestRunner()
      .expectNext(McOperation<mc_op_shutdown>(), std::move(r))
      .run("shutdown 10\r\n")
      .run("shutdown   10  \r\n");
}

TEST(McServerAsciiParserHarness, stats) {
  TestRunner()
      .expectNext(McOperation<mc_op_stats>(), McRequest())
      .run("stats\r\n")
      .run("stats    \r\n");

  multiTokenOpTest(McOperation<mc_op_stats>(), "stats");
}

TEST(McServerAsciiParserHarness, exec) {
  multiTokenOpTest(McOperation<mc_op_exec>(), "exec");
  multiTokenOpTest(McOperation<mc_op_exec>(), "admin");
}

TEST(McServerAsciiParserHarness, delete) {
  TestRunner()
      .expectNext(McOperation<mc_op_delete>(), McRequest("test:stepan:1"))
      .run("delete test:stepan:1\r\n")
      .run("delete  test:stepan:1  \r\n");
  TestRunner()
      .expectNext(McOperation<mc_op_delete>(), McRequest("test:stepan:1"),
                  true)
      .run("delete test:stepan:1 noreply\r\n")
      .run("delete  test:stepan:1  noreply   \r\n");

  McRequest r("test:stepan:1");
  r.setExptime(-10);
  TestRunner()
      .expectNext(McOperation<mc_op_delete>(), r.clone())
      .run("delete test:stepan:1 -10\r\n")
      .run("delete  test:stepan:1  -10  \r\n");

  r.setExptime(1234123);
  TestRunner()
      .expectNext(McOperation<mc_op_delete>(), r.clone())
      .run("delete test:stepan:1 1234123\r\n")
      .run("delete  test:stepan:1  1234123  \r\n");
  TestRunner()
      .expectNext(McOperation<mc_op_delete>(), r.clone(), true)
      .run("delete test:stepan:1 1234123 noreply\r\n")
      .run("delete  test:stepan:1  1234123  noreply  \r\n");
}

TEST(McServerAsciiParserHarness, incr) {
  arithmeticTest(McOperation<mc_op_incr>(), "incr");
}

TEST(McServerAsciiParserHarness, decr) {
  arithmeticTest(McOperation<mc_op_decr>(), "decr");
}

TEST(McServerAsciiParserHarness, flush_all) {
  TestRunner()
      .expectNext(McOperation<mc_op_flushall>(), McRequest())
      .run("flush_all\r\n")
      .run("flush_all     \r\n");

  McRequest r;
  r.setNumber(123456789);
  TestRunner()
      .expectNext(McOperation<mc_op_flushall>(), std::move(r))
      .run("flush_all 123456789\r\n")
      .run("flush_all    123456789\r\n")
      .run("flush_all    123456789   \r\n");
}

TEST(McServerAsciiParserHarness, flush_regex) {
  // Flush_regex expects a key.
  TestRunner()
      .expectError()
      .run("flush_regex\r\n")
      .run("flush_regex     \r\n");

  TestRunner()
      .expectNext(McOperation<mc_op_flushre>(), McRequest("test:stepan:1"))
      .run("flush_regex test:stepan:1\r\n")
      .run("flush_regex    test:stepan:1\r\n")
      .run("flush_regex   test:stepan:1   \r\n");
}

TEST(McServerAsciiParserHarness, lease_set) {
  McRequest r = createSetRequest("test:stepan:1", kTestValue, 1, 65);
  r.setLeaseToken(123);

  TestRunner()
      .expectNext(McOperation<mc_op_lease_set>(), r.clone())
      .run("lease-set test:stepan:1 123 1 65 18\r\nsomeSmallTestValue\r\n")
      .run("lease-set   test:stepan:1   123   1   65   18  \r\n"
           "someSmallTestValue\r\n");

  TestRunner()
      .expectNext(McOperation<mc_op_lease_set>(), r.clone(), true)
      .run("lease-set test:stepan:1 123 1 65 18 noreply\r\n"
           "someSmallTestValue\r\n")
      .run("lease-set   test:stepan:1   123   1   65   18  noreply  \r\n"
           "someSmallTestValue\r\n");
}

TEST(McServerAsciiParserHarness, cas) {
  McRequest r = createSetRequest("test:stepan:1", kTestValue, 1, 65);
  r.setCas(123);

  TestRunner()
      .expectNext(McOperation<mc_op_cas>(), r.clone())
      .run("cas test:stepan:1 1 65 18 123\r\nsomeSmallTestValue\r\n")
      .run("cas   test:stepan:1   1   65   18  123  \r\n"
           "someSmallTestValue\r\n");

  TestRunner()
      .expectNext(McOperation<mc_op_cas>(), r.clone(), true)
      .run("cas test:stepan:1 1 65 18 123 noreply\r\n"
           "someSmallTestValue\r\n")
      .run("cas   test:stepan:1   1   65   18   123   noreply  \r\n"
           "someSmallTestValue\r\n");
}

TEST(McServerAsciiParserHarness, allOps) {
  McRequest casRequest =
      createSetRequest("test:stepan:11", "Facebook", 765, -1);
  casRequest.setCas(893);

  McRequest leaseSetRequest =
      createSetRequest("test:stepan:12", "hAcK", 294, 563);
  leaseSetRequest.setLeaseToken(846);

  McRequest deleteRequest("test:stepan:13");
  deleteRequest.setExptime(2345234);

  TestRunner()
      .expectNext(McOperation<mc_op_get>(), McRequest("test:stepan:1"))
      .expectMultiOpEnd()
      .expectNext(McOperation<mc_op_gets>(), McRequest("test:stepan:2"))
      .expectNext(McOperation<mc_op_gets>(), McRequest("test:stepan:10"))
      .expectMultiOpEnd()
      .expectNext(McOperation<mc_op_lease_get>(), McRequest("test:stepan:3"))
      .expectMultiOpEnd()
      .expectNext(McOperation<mc_op_metaget>(), McRequest("test:stepan:4"))
      .expectMultiOpEnd()
      .expectNext(McOperation<mc_op_set>(),
                  createSetRequest("test:stepan:5", "Abc", 1, 2))
      .expectNext(McOperation<mc_op_add>(),
                  createSetRequest("test:stepan:6", "abcdefgHiJklMNo", 3, 4))
      .expectNext(McOperation<mc_op_replace>(),
                  createSetRequest("test:stepan:7", "A", 6, 7))
      .expectNext(McOperation<mc_op_append>(),
                  createSetRequest("test:stepan:8", "", 8, 9))
      .expectNext(McOperation<mc_op_prepend>(),
                  createSetRequest("test:stepan:9", "xYZ", 10, 11))
      .expectNext(McOperation<mc_op_cas>(), casRequest.clone())
      .expectNext(McOperation<mc_op_lease_set>(), leaseSetRequest.clone())
      .expectNext(McOperation<mc_op_delete>(), deleteRequest.clone())
      .expectNext(McOperation<mc_op_stats>(), McRequest("test stats"))
      .expectNext(McOperation<mc_op_exec>(), McRequest("reboot server"))
      .expectNext(McOperation<mc_op_quit>(), McRequest(), true)
      .expectNext(McOperation<mc_op_version>(), McRequest())
      .expectNext(McOperation<mc_op_shutdown>(), McRequest())
      .expectNext(McOperation<mc_op_incr>(), createArithmRequest("arithm!", 90))
      .expectNext(McOperation<mc_op_decr>(), createArithmRequest("ArItHm!", 87))
      .expectNext(McOperation<mc_op_flushall>(), McRequest())
      .expectNext(McOperation<mc_op_flushre>(), McRequest("^reGex$"))
      .run(
          "get test:stepan:1\r\n"
          "gets test:stepan:2 test:stepan:10\r\n"
          "lease-get test:stepan:3\r\n"
          "metaget test:stepan:4\r\n"
          "set test:stepan:5 1 2 3\r\nAbc\r\n"
          "add test:stepan:6 3 4 15\r\nabcdefgHiJklMNo\r\n"
          "replace test:stepan:7 6 7 1\r\nA\r\n"
          "append test:stepan:8 8 9 0\r\n\r\n"
          "prepend test:stepan:9 10 11 3\r\nxYZ\r\n"
          "cas test:stepan:11 765 -1 8 893\r\nFacebook\r\n"
          "lease-set test:stepan:12 846 294 563 4\r\nhAcK\r\n"
          "delete test:stepan:13 2345234\r\n"
          "stats test stats\r\n"
          "exec reboot server\r\n"
          "quit\r\n"
          "version\r\n"
          "shutdown\n"
          "incr arithm! 90\r\n"
          "decr ArItHm! 87\r\n"
          "flush_all\r\n"
          "flush_regex ^reGex$\r\n");
}
