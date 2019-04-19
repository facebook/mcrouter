/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include <signal.h>

#include <iostream>
#include <thread>

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/Singleton.h>
#include <folly/logging/Init.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp2/transport/rsocket/server/RSRoutingHandler.h>

#include "mcrouter/lib/network/gen/MemcacheMessages.h"
#include "mcrouter/lib/network/gen/MemcacheServer.h"
#include "mcrouter/lib/network/gen/gen-cpp2/Memcache.h"
#include "mcrouter/lib/network/test/MockMc.h"
#include "mcrouter/lib/network/test/MockMcOnRequest.h"

/**
 * Mock Memcached implementation over Thrift.
 *
 * The purpose of this program is to serve as a Memcached over thrift mock for
 * other project's integration tests;
 *
 * The intention is to have the same semantics as our Memcached fork.
 *
 * Certain keys with __mockmc__. prefix provide extra functionality
 * useful for testing.
 */

using namespace facebook::memcache;

template <class Reply>
class ThriftContext {
 public:
  explicit ThriftContext(
      std::unique_ptr<apache::thrift::HandlerCallback<Reply>> callback)
      : callback_(std::move(callback)) {}

  static void reply(ThriftContext&& ctx, Reply&& reply) {
    decltype(ctx.callback_)::element_type::resultInThread(
        std::move(ctx.callback_), std::move(reply));
  }

 private:
  std::unique_ptr<apache::thrift::HandlerCallback<Reply>> callback_;
};

class ThriftHandler : virtual public facebook::memcache::thrift::MemcacheSvIf {
 public:
  void async_eb_mcGet(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McGetReply>> callback,
      const facebook::memcache::McGetRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcSet(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McSetReply>> callback,
      const facebook::memcache::McSetRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcDelete(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McDeleteReply>> callback,
      const facebook::memcache::McDeleteRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcLeaseGet(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McLeaseGetReply>> callback,
      const facebook::memcache::McLeaseGetRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcLeaseSet(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McLeaseSetReply>> callback,
      const facebook::memcache::McLeaseSetRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcAdd(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McAddReply>> callback,
      const facebook::memcache::McAddRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcReplace(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McReplaceReply>> callback,
      const facebook::memcache::McReplaceRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcGets(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McGetsReply>> callback,
      const facebook::memcache::McGetsRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcCas(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McCasReply>> callback,
      const facebook::memcache::McCasRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcIncr(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McIncrReply>> callback,
      const facebook::memcache::McIncrRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcDecr(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McDecrReply>> callback,
      const facebook::memcache::McDecrRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcMetaget(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McMetagetReply>> callback,
      const facebook::memcache::McMetagetRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcAppend(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McAppendReply>> callback,
      const facebook::memcache::McAppendRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcPrepend(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McPrependReply>> callback,
      const facebook::memcache::McPrependRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcTouch(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McTouchReply>> callback,
      const facebook::memcache::McTouchRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcFlushRe(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McFlushReReply>> callback,
      const facebook::memcache::McFlushReRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcFlushAll(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McFlushAllReply>> callback,
      const facebook::memcache::McFlushAllRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcGat(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McGatReply>> callback,
      const facebook::memcache::McGatRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcGats(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McGatsReply>> callback,
      const facebook::memcache::McGatsRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

  virtual void async_eb_mcVersion(
      std::unique_ptr<apache::thrift::HandlerCallback<
          facebook::memcache::McVersionReply>> callback,
      const facebook::memcache::McVersionRequest& request) override final {
    auto reqCopy = request;
    onRequest_.onRequest(
        ThriftContext(std::move(callback)), std::move(reqCopy));
  }

 private:
  MockMcOnRequest onRequest_;
};

[[noreturn]] void usage(char** argv) {
  std::cerr << "Arguments:\n"
               "  -P <port>      TCP port on which to listen\n"
               "  -t <fd>        TCP listen sock fd\n"
               "Usage:\n"
               "  $ "
            << argv[0] << " -p 15213\n";
  exit(1);
}

// Configure folly to enable INFO+ messages, and everything else to
// enable WARNING+.
// Set the default log handler to log asynchronously by default.
FOLLY_INIT_LOGGING_CONFIG(".=WARNING,folly=INFO; default:async=true");

std::shared_ptr<apache::thrift::ThriftServer> gServer;

void sigHandler(int /* signo */) {
  gServer->stop();
}

int main(int argc, char** argv) {
  folly::SingletonVault::singleton()->registrationComplete();

  uint16_t port = 0;
  int existingSocketFd = 0;

  int c;
  while ((c = getopt(argc, argv, "P:t:h")) >= 0) {
    switch (c) {
      case 'P':
        port = folly::to<uint16_t>(optarg);
        break;
      case 't':
        existingSocketFd = folly::to<int>(optarg);
        break;
      default:
        usage(argv);
    }
  }

  try {
    LOG(INFO) << "Starting thrift server.";
    gServer = std::make_shared<apache::thrift::ThriftServer>();
    auto handler = std::make_shared<ThriftHandler>();
    gServer->setInterface(handler);
    if (port > 0) {
      gServer->setPort(port);
    } else if (existingSocketFd > 0) {
      gServer->useExistingSocket(existingSocketFd);
    }
    gServer->enableRocketServer(true);
    gServer->setNumIOWorkerThreads(2);
    gServer->addRoutingHandler(
        std::make_unique<apache::thrift::RSRoutingHandler>());
    signal(SIGINT, sigHandler);
    gServer->serve();
    LOG(INFO) << "Shutting down thrift server.";
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
}
