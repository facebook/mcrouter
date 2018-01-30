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

#include <folly/dynamic.h>
#include <folly/json.h>

#include "mcrouter/lib/carbon/CmdLineClient.h"
#include "mcrouter/lib/carbon/JsonClient.h"
#include "mcrouter/lib/carbon/test/gen/CarbonTest.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/test/ListenSocket.h"

using carbon::JsonClient;
using carbon::tools::CmdLineClient;
using namespace facebook::memcache;

namespace carbon {
namespace test {

namespace {

class TestJsonClient : public JsonClient {
 public:
  explicit TestJsonClient(
      JsonClient::Options opts,
      std::function<void(const std::string&)> onError = nullptr)
      : JsonClient(std::move(opts), std::move(onError)) {}

 protected:
  bool sendRequestByName(
      const std::string& requestName,
      const folly::dynamic& requestJson,
      folly::dynamic& replyJson) final {
    if (requestName == TestRequest::name) {
      return sendRequest<TestRequest>(requestJson, replyJson);
    } else if (requestName == TestRequestStringKey::name) {
      return sendRequest<TestRequestStringKey>(requestJson, replyJson);
    }
    return false;
  }
};

struct CarbonTestOnRequest {
  void onRequest(McServerRequestContext&& ctx, TestRequest&& request) {
    TestReply reply(mc_res_ok);
    reply.valInt32() = request.testInt32() * 2;
    reply.valInt64() = request.testInt64() * 2;
    McServerRequestContext::reply(std::move(ctx), std::move(reply));
  }
  void onRequest(
      McServerRequestContext&& ctx,
      TestRequestStringKey&& /* request */) {
    McServerRequestContext::reply(
        std::move(ctx), TestReplyStringKey(mc_res_notfound));
  }
};

std::unique_ptr<AsyncMcServer> startServer(int existingSocketFd) {
  AsyncMcServer::Options opts;
  opts.existingSocketFd = existingSocketFd;
  opts.numThreads = 1;

  auto server = std::make_unique<AsyncMcServer>(std::move(opts));

  server->spawn(
      [](size_t /* threadId */,
         folly::EventBase& evb,
         AsyncMcServerWorker& worker) {
        worker.setOnRequest(CarbonTestRequestHandler<CarbonTestOnRequest>());

        while (worker.isAlive() || worker.writesPending()) {
          evb.loopOnce();
        }
      });

  return server;
}

JsonClient::Options getClientOpts(
    uint16_t port,
    bool ignoreParsingErrors = true) {
  JsonClient::Options opts;
  opts.port = port;
  opts.ignoreParsingErrors = ignoreParsingErrors;
  return opts;
}

template <class Type>
inline void checkIntField(
    const folly::dynamic& json,
    folly::StringPiece name,
    Type expected) {
  ASSERT_EQ(1, json.count(name));
  ASSERT_TRUE(json[name].isInt());
  EXPECT_EQ(static_cast<int64_t>(expected), json[name].asInt());
}

} // anonymous namespace

TEST(JsonClient, sendRequests) {
  ListenSocket socket;
  auto server = startServer(socket.getSocketFd());
  TestJsonClient client(getClientOpts(socket.getPort()));

  const std::string payload = R"json(
    {
      "key": "abcdef",
      "testBool": true,
      "testInt32": 17,
      "testInt64": 30
    }
  )json";

  folly::dynamic reply;
  auto result = client.sendRequests("test", folly::parseJson(payload), reply);
  ASSERT_TRUE(result);
  ASSERT_TRUE(reply.isObject());

  checkIntField(reply, "result", mc_res_ok);
  checkIntField(reply, "valInt32", 34);
  checkIntField(reply, "valInt64", 60);

  server->shutdown();
  server->join();
}

TEST(JsonClient, sendRequests_Array) {
  ListenSocket socket;
  auto server = startServer(socket.getSocketFd());
  TestJsonClient client(getClientOpts(socket.getPort()));

  const std::string payload = R"json(
    [
      {
        "key": "abc",
        "testInt32": 10,
        "testInt64": 30
      },
      {
        "key": "def",
        "testInt32": 35,
        "testInt64": 50
      }
    ]
  )json";

  folly::dynamic replies;
  auto result = client.sendRequests("test", folly::parseJson(payload), replies);
  ASSERT_TRUE(result);
  ASSERT_TRUE(replies.isArray());

  checkIntField(replies[0], "result", mc_res_ok);
  checkIntField(replies[0], "valInt32", 20);
  checkIntField(replies[0], "valInt64", 60);

  checkIntField(replies[1], "result", mc_res_ok);
  checkIntField(replies[1], "valInt32", 70);
  checkIntField(replies[1], "valInt64", 100);

  server->shutdown();
  server->join();
}

TEST(CmdLineClient, sendRequests) {
  ListenSocket socket;
  auto server = startServer(socket.getSocketFd());
  auto portStr = folly::to<std::string>(socket.getPort());

  std::array<const char*, 7> argv = {{"binaryName",
                                      "-h",
                                      "localhost",
                                      "-p",
                                      portStr.c_str(),
                                      "test", // Request name
                                      R"json(
        {
          "key": "abcdef",
          "testBool": true,
          "testInt32": 17,
          "testInt64": 30
        }
      )json"}};

  std::stringstream outStream(std::ios::out | std::ios::in);
  std::stringstream errStream(std::ios::out | std::ios::in);

  CmdLineClient client(outStream, errStream);
  client.sendRequests<TestJsonClient>(argv.size(), argv.data());

  auto out = outStream.str();
  auto err = errStream.str();

  EXPECT_EQ(0, err.size());
  EXPECT_GT(out.size(), 0);

  auto reply = folly::parseJson(out);

  checkIntField(reply, "result", mc_res_ok);
  checkIntField(reply, "valInt32", 34);
  checkIntField(reply, "valInt64", 60);

  server->shutdown();
  server->join();
}

TEST(CmdLineClient, sendRequests_InvalidJson) {
  ListenSocket socket;
  auto server = startServer(socket.getSocketFd());
  auto portStr = folly::to<std::string>(socket.getPort());

  std::array<const char*, 7> argv = {{"binaryName",
                                      "-h",
                                      "localhost",
                                      "-p",
                                      portStr.c_str(),
                                      "test", // Request name
                                      "Invalid json."}};

  std::stringstream outStream(std::ios::out | std::ios::in);
  std::stringstream errStream(std::ios::out | std::ios::in);

  CmdLineClient client(outStream, errStream);
  client.sendRequests<TestJsonClient>(argv.size(), argv.data());

  auto out = outStream.str();
  auto err = errStream.str();

  EXPECT_EQ(0, out.size());
  EXPECT_GT(err.size(), 0);
  std::cerr << err << std::endl;

  server->shutdown();
  server->join();
}

TEST(CmdLineClient, sendRequests_InvalidRequestName) {
  ListenSocket socket;
  auto server = startServer(socket.getSocketFd());
  auto portStr = folly::to<std::string>(socket.getPort());

  std::array<const char*, 7> argv = {{"binaryName",
                                      "-h",
                                      "localhost",
                                      "-p",
                                      portStr.c_str(),
                                      "abc", // Request name
                                      R"json(
        {
          "key": "abcdef",
          "testBool": true,
          "testInt32": 17,
          "testInt64": 30
        }
      )json"}};

  std::stringstream outStream(std::ios::out | std::ios::in);
  std::stringstream errStream(std::ios::out | std::ios::in);

  CmdLineClient client(outStream, errStream);
  client.sendRequests<TestJsonClient>(argv.size(), argv.data());

  auto out = outStream.str();
  auto err = errStream.str();

  EXPECT_EQ(0, out.size());
  EXPECT_GT(err.size(), 0);
  std::cerr << err << std::endl;

  server->shutdown();
  server->join();
}

} // test
} // carbon
