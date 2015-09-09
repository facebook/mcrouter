/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <gtest/gtest.h>

#include <folly/experimental/fibers/EventBaseLoopController.h>
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/test/TestUtil.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

namespace facebook { namespace memcache {

struct CommonStats {
  std::atomic<int> accepted{0};
};

inline const char* getPemKeyPath() {
  static const char* kPemKeyPath = "mcrouter/lib/network/test/test_key.pem";
  return kPemKeyPath;
}

inline const char* getPemCertPath() {
  static const char* kPemCertPath = "mcrouter/lib/network/test/test_cert.pem";
  return kPemCertPath;
}

inline const char* getPemCaPath() {
  static const char* kPemCaPath = "mcrouter/lib/network/test/ca_cert.pem";
      return kPemCaPath;
}

class TestServerOnRequest {
 public:
  TestServerOnRequest(bool& shutdown, bool outOfOrder) :
      shutdown_(shutdown),
      outOfOrder_(outOfOrder) {
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<mc_op_get>) {
    if (req.fullKey() == "sleep") {
      /* sleep override */ usleep(1000000);
      processReply(std::move(ctx), McReply(mc_res_notfound));
    } else if (req.fullKey() == "shutdown") {
      shutdown_ = true;
      processReply(std::move(ctx), McReply(mc_res_notfound));
      flushQueue();
    } else {
      std::string value = req.fullKey() == "empty" ? "" : req.fullKey().str();
      McReply foundReply = McReply(mc_res_found, createMcMsgRef(req.fullKey(),
                                                                value));
      if (req.fullKey() == "hold") {
        waitingReplies_.emplace_back(std::move(ctx), std::move(foundReply));
      } else if (req.fullKey() == "flush") {
        processReply(std::move(ctx), std::move(foundReply));
        flushQueue();
      } else {
        processReply(std::move(ctx), std::move(foundReply));
      }
    }
  }

  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<mc_op_set>) {
    processReply(std::move(ctx), McReply(mc_res_stored));
  }

  template <int M>
  void onRequest(McServerRequestContext&& ctx,
                 McRequest&& req,
                 McOperation<M>) {
    LOG(ERROR) << "Unhandled operation " << M;
  }

  void processReply(McServerRequestContext&& context, McReply&& reply) {
    if (outOfOrder_) {
      McServerRequestContext::reply(std::move(context), std::move(reply));
    } else {
      waitingReplies_.emplace_back(std::move(context), std::move(reply));
      if (waitingReplies_.size() == 1) {
        flushQueue();
      }
    }
  }

  void flushQueue() {
    for (size_t i = 0; i < waitingReplies_.size(); ++i) {
      McServerRequestContext::reply(std::move(waitingReplies_[i].first),
                                    std::move(waitingReplies_[i].second));
    }
    waitingReplies_.clear();
  }

 private:
  bool& shutdown_;
  bool outOfOrder_;
  std::vector<std::pair<McServerRequestContext, McReply>> waitingReplies_;
};

template <class OnRequest>
class TestServer {
 public:
  TestServer(bool outOfOrder, bool useSsl,
             int maxInflight = 10, int timeoutMs = 250, size_t maxConns = 100,
             size_t unreapableTime = 0, size_t updateThreshold = 0) :
      outOfOrder_(outOfOrder) {
    socketFd_ = createListenSocket();
    opts_.existingSocketFd = socketFd_;
    opts_.numThreads = 1;
    opts_.worker.maxInFlight = maxInflight;
    opts_.worker.sendTimeout = std::chrono::milliseconds{timeoutMs};
    opts_.worker.connLRUopts.maxConns =
      (maxConns + opts_.numThreads - 1) / opts_.numThreads;
    opts_.worker.connLRUopts.updateThreshold =
      std::chrono::milliseconds(updateThreshold);
    opts_.worker.connLRUopts.unreapableTime =
      std::chrono::milliseconds(unreapableTime);
    if (useSsl) {
      opts_.pemKeyPath = getPemKeyPath();
      opts_.pemCertPath = getPemCertPath();
      opts_.pemCaPath = getPemCaPath();
    }
    EXPECT_TRUE(run());
    // allow server some time to startup
    /* sleep override */ usleep(100000);
  }

  uint16_t getListenPort() const {
    return facebook::memcache::getListenPort(socketFd_);
  }

  bool run() {
    try {
      LOG(INFO) << "Spawning AsyncMcServer";

      server_ = folly::make_unique<AsyncMcServer>(opts_);
      server_->spawn(
        [this] (size_t threadId,
                folly::EventBase& evb,
                AsyncMcServerWorker& worker) {

          bool shutdown = false;
          worker.setOnRequest(OnRequest(shutdown, outOfOrder_));
          worker.setOnConnectionAccepted([this] () {
            ++stats_.accepted;
          });

          while (!shutdown) {
            evb.loopOnce();
          }

          LOG(INFO) << "Shutting down AsyncMcServer";

          worker.shutdown();
        });

      return true;
    } catch (const folly::AsyncSocketException& e) {
      LOG(ERROR) << e.what();
      return false;
    }
  }

  void join() {
    server_->join();
  }

  CommonStats& getStats() {
    return stats_;
  }
 private:
  int socketFd_;
  AsyncMcServer::Options opts_;
  std::unique_ptr<AsyncMcServer> server_;
  bool outOfOrder_ = false;
  CommonStats stats_;
};

class TestClient {
 public:
  TestClient(std::string host,
             uint16_t port,
             int timeoutMs,
             mc_protocol_t protocol = mc_ascii_protocol,
             bool useSsl = false,
             std::function<std::shared_ptr<folly::SSLContext>()>
                 contextProvider = nullptr,
             bool enableQoS = false,
             uint64_t qosClass = 0,
             uint64_t qosPath = 0,
             bool useTyped = false)
      : fm_(folly::make_unique<folly::fibers::EventBaseLoopController>()) {
    dynamic_cast<folly::fibers::EventBaseLoopController&>(fm_.loopController()).
      attachEventBase(eventBase_);
    ConnectionOptions opts(host, port, protocol);
    opts.useTyped = useTyped;
    opts.writeTimeout = std::chrono::milliseconds(timeoutMs);
    if (useSsl) {
      auto defaultContextProvider = [] () {
        return getSSLContext(getPemCertPath(), getPemKeyPath(), getPemCaPath());
      };
      opts.sslContextProvider = contextProvider
        ? contextProvider
        : defaultContextProvider;
    }
    if (enableQoS) {
      opts.enableQoS = true;
      opts.qosClass = qosClass;
      opts.qosPath = qosPath;
    }
    client_ = folly::make_unique<AsyncMcClient>(eventBase_, opts);
    client_->setStatusCallbacks([] { LOG(INFO) << "Client UP."; },
                                [] (bool) { LOG(INFO) << "Client DOWN."; });
  }

  void setThrottle(size_t maxInflight, size_t maxOutstanding) {
    client_->setThrottle(maxInflight, maxOutstanding);
  }

  void setStatusCallbacks(std::function<void()> onUp,
      std::function<void(bool aborting)> onDown) {
    client_->setStatusCallbacks(
       [onUp] {
          LOG(INFO) << "Client UP.";
          onUp();
       },
       [onDown] (bool aborting) {
          LOG(INFO) << "Client DOWN.";
          onDown(aborting);
       });
  }

  void sendGet(const char* key, mc_res_t expectedResult,
               uint32_t timeoutMs = 200) {
    inflight_++;
    std::string K(key);
    fm_.addTask([K, expectedResult, this, timeoutMs]() {
        auto msg = createMcMsgRef(K.c_str());
        msg->op = mc_op_get;
        McRequest req{std::move(msg)};
        try {
          auto reply = client_->sendSync(req, McOperation<mc_op_get>(),
                                         std::chrono::milliseconds(timeoutMs));
          if (reply.result() == mc_res_found) {
            if (req.fullKey() == "empty") {
              EXPECT_TRUE(reply.hasValue());
              EXPECT_EQ("", toString(reply.value()));
            } else {
              EXPECT_EQ(toString(reply.value()), req.fullKey());
            }
          }
          EXPECT_STREQ(mc_res_to_string(expectedResult),
                       mc_res_to_string(reply.result()));
        } catch (const std::exception& e) {
          LOG(ERROR) << e.what();
          CHECK(false);
        }
        inflight_--;
      });
  }

  void sendSet(const char* key, const char* value, mc_res_t expectedResult) {
    inflight_++;
    std::string K(key);
    std::string V(value);
    fm_.addTask([K, V, expectedResult, this]() {
        auto msg = createMcMsgRef(K.c_str(), V.c_str());
        msg->op = mc_op_set;
        McRequest req{std::move(msg)};

        auto reply = client_->sendSync(req, McOperation<mc_op_set>(),
                                       std::chrono::milliseconds(200));

        EXPECT_STREQ(mc_res_to_string(expectedResult),
                     mc_res_to_string(reply.result()));

        inflight_--;
      });

  }

  size_t getOutstandingCount() const {
    return inflight_;
  }

  /**
   * Wait until there're more than remaining requests in queue.
   */
  void waitForReplies(size_t remaining = 0) {
    while (getOutstandingCount() > remaining) {
      loopOnce();
    }
  }

  /**
   * Loop once client EventBase, will cause it to write requests into socket.
   */
  void loopOnce() {
    eventBase_.loopOnce();
  }

  AsyncMcClient& getClient() {
    return *client_;
  }

  folly::EventBase eventBase_;
 private:
  size_t inflight_{0};
  std::unique_ptr<AsyncMcClient> client_;
  folly::fibers::FiberManager fm_;
};

inline std::string genBigValue() {
  const size_t kBigValueSize = 1024 * 1024 * 4;
  std::string bigValue(kBigValueSize, '.');
  for (size_t i = 0; i < kBigValueSize; ++i) {
    bigValue[i] = 65 + (i % 26);
  }
  return bigValue;
}

}} // facebook::memcache
