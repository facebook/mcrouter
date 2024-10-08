/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */

/*
 *  THIS FILE IS AUTOGENERATED. DO NOT MODIFY IT; ALL CHANGES WILL BE LOST IN
 *  VAIN.
 *
 *  @generated
 */
#include <folly/init/Init.h>

#include <mcrouter/lib/carbon/CmdLineClient.h>
#include <mcrouter/lib/carbon/JsonClient.h>

#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeMessages.h"
#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeRouterInfo.h"

using carbon::JsonClient;
using carbon::tools::CmdLineClient;

namespace {

class HelloGoodbyeJsonClient : public JsonClient {
 public:
  explicit HelloGoodbyeJsonClient(
      JsonClient::Options opts,
      std::function<void(const std::string&)> onError)
      : JsonClient(std::move(opts), std::move(onError)) {}

 protected:
  bool sendRequestByName(
      const std::string& requestName,
      const folly::dynamic& requestJson,
      folly::dynamic& replyJson) override final {
    if (requestName == facebook::memcache::McExecRequest::name) {
      return sendRequest<facebook::memcache::McExecRequest>(requestJson, replyJson);
    }
    if (requestName == facebook::memcache::McQuitRequest::name) {
      return sendRequest<facebook::memcache::McQuitRequest>(requestJson, replyJson);
    }
    if (requestName == facebook::memcache::McShutdownRequest::name) {
      return sendRequest<facebook::memcache::McShutdownRequest>(requestJson, replyJson);
    }
    if (requestName == facebook::memcache::McStatsRequest::name) {
      return sendRequest<facebook::memcache::McStatsRequest>(requestJson, replyJson);
    }
    if (requestName == facebook::memcache::McVersionRequest::name) {
      return sendRequest<facebook::memcache::McVersionRequest>(requestJson, replyJson);
    }
    if (requestName == hellogoodbye::GoodbyeRequest::name) {
      return sendRequest<hellogoodbye::GoodbyeRequest>(requestJson, replyJson);
    }
    if (requestName == hellogoodbye::HelloRequest::name) {
      return sendRequest<hellogoodbye::HelloRequest>(requestJson, replyJson);
    }
    return false;
  }
};
} // anonymous namespace

int main(int argc, char** argv) {
  int tmpArgc = 1;
  folly::init(&tmpArgc, &argv, /* removeFlags */ false);
  CmdLineClient client;
  client.sendRequests<HelloGoodbyeJsonClient>(argc, const_cast<const char**>(argv));
  return 0;
}
