/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TestClientServerUtil.h"

#include <chrono>
#include <string>
#include <thread>

#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/experimental/fibers/EventBaseLoopController.h>
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/test/ListenSocket.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"

namespace facebook { namespace memcache { namespace test {

const char* const kPemCaPath = "mcrouter/lib/network/test/ca_cert.pem";
const char* const kValidKeyPath = "mcrouter/lib/network/test/test_key.pem";
const char* const kValidCertPath = "mcrouter/lib/network/test/test_cert.pem";

const char* const kBrokenKeyPath = "mcrouter/lib/network/test/broken_key.pem";
const char* const kBrokenCertPath = "mcrouter/lib/network/test/broken_cert.pem";

const char* const kInvalidKeyPath = "/do/not/exist";
const char* const kInvalidCertPath = "/do/not/exist";

const char* const kServerVersion = "TestServer-1.0";

SSLContextProvider validSsl() {
  return []() {
    return getSSLContext(kValidCertPath, kValidKeyPath, kPemCaPath);
  };
}

SSLContextProvider invalidSsl() {
  return []() {
    return getSSLContext(kInvalidCertPath, kInvalidKeyPath, kPemCaPath);
  };
}

SSLContextProvider brokenSsl() {
  return []() {
    return getSSLContext(kBrokenCertPath, kBrokenKeyPath, kPemCaPath);
  };
}

TestServerOnRequest::TestServerOnRequest(std::atomic<bool>& shutdown,
                                         bool outOfOrder)
    : shutdown_(shutdown), outOfOrder_(outOfOrder) {}

void TestServerOnRequest::onRequest(
    McServerRequestContext&& ctx,
    McRequestWithMcOp<mc_op_get>&& req) {

  if (req.fullKey() == "sleep") {
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::seconds(1));
    processReply(std::move(ctx), McReply(mc_res_notfound));
  } else if (req.fullKey() == "shutdown") {
    shutdown_.store(true);
    processReply(std::move(ctx), McReply(mc_res_notfound));
    flushQueue();
  } else if (req.fullKey() == "busy") {
    processReply(std::move(ctx), McReply(mc_res_busy));
  } else {
    std::string value;
    if (req.fullKey().startsWith("value_size:")) {
      auto key = req.fullKey();
      key.removePrefix("value_size:");
      size_t valSize = folly::to<size_t>(key);
      value = std::string(valSize, 'a');
    } else if (req.fullKey() != "empty") {
      value = req.fullKey().str();
    }
    McReply foundReply = McReply(mc_res_found, createMcMsgRef(req.fullKey(),
                                                              value));
    if (req.fullKey() == "hold") {
      waitingReplies_.push_back(
        [ctx = std::move(ctx), reply = std::move(foundReply)]() mutable {
         McServerRequestContext::reply(std::move(ctx), std::move(reply));
        });
    } else if (req.fullKey() == "flush") {
      processReply(std::move(ctx), std::move(foundReply));
      flushQueue();
    } else {
      processReply(std::move(ctx), std::move(foundReply));
    }
  }
}

void TestServerOnRequest::onRequest(
    McServerRequestContext&& ctx,
    TypedThriftRequest<cpp2::McGetRequest>&& req) {
  using Reply = TypedThriftReply<cpp2::McGetReply>;

  if (req.fullKey() == "sleep") {
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::seconds(1));
    processReply(std::move(ctx), Reply(mc_res_notfound));
  } else if (req.fullKey() == "shutdown") {
    shutdown_.store(true);
    processReply(std::move(ctx), Reply(mc_res_notfound));
    flushQueue();
  } else if (req.fullKey() == "busy") {
    processReply(std::move(ctx), Reply(mc_res_busy));
  } else {
    std::string value;
    if (req.fullKey().startsWith("value_size:")) {
      auto key = req.fullKey();
      key.removePrefix("value_size:");
      size_t valSize = folly::to<size_t>(key);
      value = std::string(valSize, 'a');
    } else if (req.fullKey() == "trace_id") {
      value = folly::to<std::string>(req.traceId());
    } else if (req.fullKey() != "empty") {
      value = req.fullKey().str();
    }

    Reply foundReply(mc_res_found);
    foundReply.setValue(value);

    if (req.fullKey() == "hold") {
      waitingReplies_.push_back(
        [ctx = std::move(ctx), reply = std::move(foundReply)]() mutable {
         McServerRequestContext::reply(std::move(ctx), std::move(reply));
        });
    } else if (req.fullKey() == "flush") {
      processReply(std::move(ctx), std::move(foundReply));
      flushQueue();
    } else {
      processReply(std::move(ctx), std::move(foundReply));
    }
  }
}

void TestServerOnRequest::onRequest(McServerRequestContext&& ctx,
                                    McRequestWithMcOp<mc_op_set>&&) {
  processReply(std::move(ctx), McReply(mc_res_stored));
}

void TestServerOnRequest::onRequest(McServerRequestContext&& ctx,
                                    TypedThriftRequest<cpp2::McSetRequest>&&) {
  processReply(
      std::move(ctx), TypedThriftReply<cpp2::McSetReply>(mc_res_stored));
}

void TestServerOnRequest::onRequest(
    McServerRequestContext&& ctx,
    TypedThriftRequest<cpp2::McVersionRequest>&&) {
  TypedThriftReply<cpp2::McVersionReply> reply(mc_res_ok);
  reply.setValue(kServerVersion);
  processReply(std::move(ctx), std::move(reply));
}

void TestServerOnRequest::flushQueue() {
  for (size_t i = 0; i < waitingReplies_.size(); ++i) {
    waitingReplies_[i]();
  }
  waitingReplies_.clear();
}

TestServer::TestServer(bool outOfOrder, bool useSsl, int maxInflight,
                       int timeoutMs, size_t maxConns, bool useDefaultVersion,
                       size_t numThreads)
      : outOfOrder_(outOfOrder) {
  opts_.existingSocketFd = sock_.getSocketFd();
  opts_.numThreads = numThreads;
  opts_.worker.defaultVersionHandler = useDefaultVersion;
  opts_.worker.maxInFlight = maxInflight;
  opts_.worker.sendTimeout = std::chrono::milliseconds{timeoutMs};
  opts_.setPerThreadMaxConns(maxConns, opts_.numThreads);
  if (useSsl) {
    opts_.pemKeyPath = kValidKeyPath;
    opts_.pemCertPath = kValidCertPath;
    opts_.pemCaPath = kPemCaPath;
  }
}

void TestServer::run(std::function<void(AsyncMcServerWorker&)> init) {
  LOG(INFO) << "Spawning AsyncMcServer";

  server_ = folly::make_unique<AsyncMcServer>(opts_);
  server_->spawn(
    [this, init](size_t, folly::EventBase& evb, AsyncMcServerWorker& worker) {
      init(worker);
      worker.setOnConnectionAccepted([this] () {
        ++acceptedConns_;
      });

      while (!shutdown_.load()) {
        evb.loopOnce(EVLOOP_NONBLOCK);
      }

      LOG(INFO) << "Shutting down AsyncMcServer";

      worker.shutdown();
    });

  // allow server some time to startup
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

std::string TestServer::version() const {
  if (opts_.worker.defaultVersionHandler) {
    return opts_.worker.versionString;
  } else {
    return kServerVersion;
  }
}

TestClient::TestClient(std::string host,
                       uint16_t port,
                       int timeoutMs,
                       mc_protocol_t protocol,
                       SSLContextProvider ssl,
                       uint64_t qosClass,
                       uint64_t qosPath)
      : fm_(folly::make_unique<folly::fibers::EventBaseLoopController>()) {
  dynamic_cast<folly::fibers::EventBaseLoopController&>(fm_.loopController()).
    attachEventBase(eventBase_);
  ConnectionOptions opts(host, port, protocol);
  opts.writeTimeout = std::chrono::milliseconds(timeoutMs);
  if (ssl) {
    opts.sslContextProvider = std::move(ssl);
    opts.sessionCachingEnabled = true;
  }
  if (qosClass != 0 || qosPath != 0) {
    opts.enableQoS = true;
    opts.qosClass = qosClass;
    opts.qosPath = qosPath;
  }
  client_ = folly::make_unique<AsyncMcClient>(eventBase_, opts);
  client_->setStatusCallbacks([] { LOG(INFO) << "Client UP."; },
                              [] (bool) { LOG(INFO) << "Client DOWN."; });
  client_->setRequestStatusCallbacks(
    [this](int pendingDiff, int inflightDiff) {
      CHECK(pendingDiff != inflightDiff)
        << "A request can't be pending and inflight at the same time";

      pendingStat_ += pendingDiff;
      inflightStat_ += inflightDiff;

      CHECK(pendingStat_ >= 0 && inflightStat_ >= 0)
        << "Pending and inflight stats should always be 0 or more.";

      pendingStatMax_ = std::max(pendingStatMax_, pendingStat_);
      inflightStatMax_ = std::max(inflightStatMax_, inflightStat_);
    },
    nullptr);
}

void TestClient::setStatusCallbacks(std::function<void()> onUp,
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

void TestClient::sendGet(std::string key, mc_res_t expectedResult,
                         uint32_t timeoutMs) {
  inflight_++;
  fm_.addTask([key, expectedResult, this, timeoutMs]() {
      TypedThriftRequest<cpp2::McGetRequest> req(key);
      if (req.fullKey() == "trace_id") {
        req.setTraceId(12345);
      }

      try {
        auto reply = client_->sendSync(req,
                                       std::chrono::milliseconds(timeoutMs));
        if (reply.result() == mc_res_found) {
          auto value = reply.valueRangeSlow();
          if (req.fullKey() == "empty") {
            checkLogic(reply.hasValue(), "Reply has no value");
            checkLogic(value.empty(), "Expected empty value, got {}", value);
          } else if (req.fullKey().startsWith("value_size:")) {
            auto key = req.fullKey();
            key.removePrefix("value_size:");
            size_t valSize = folly::to<size_t>(key);
            checkLogic(value.size() == valSize,
                       "Expected value of size {}, got {}",
                       valSize, value.size());
          } else if (req.fullKey() == "trace_id") {
            checkLogic(value == "12345",
                       "Expected value to equal trace ID {}, got {}",
                       12345, value);
          } else {
            checkLogic(value == req.fullKey(),
                       "Expected {}, got {}", req.fullKey(), value);
          }
        }
        checkLogic(expectedResult == reply.result(), "Expected {}, got {}",
                   mc_res_to_string(expectedResult),
                   mc_res_to_string(reply.result()));
      } catch (const std::exception& e) {
        CHECK(false) << "Failed: " << e.what();
      }
      inflight_--;
    });
}

void TestClient::sendSet(std::string key, std::string value,
                         mc_res_t expectedResult) {
  inflight_++;
  fm_.addTask([key, value, expectedResult, this]() {
      TypedThriftRequest<cpp2::McSetRequest> req(key);
      req.setValue(value);

      auto reply = client_->sendSync(req, std::chrono::milliseconds(200));

      CHECK(expectedResult == reply.result())
        << "Expected: " << mc_res_to_string(expectedResult)
        << " got " << mc_res_to_string(reply.result());

      inflight_--;
    });
}

void TestClient::sendVersion(std::string expectedVersion) {
  ++inflight_;
  fm_.addTask([this, expectedVersion = std::move(expectedVersion)]() {
      TypedThriftRequest<cpp2::McVersionRequest> req;

      auto reply = client_->sendSync(req, std::chrono::milliseconds(200));

      CHECK_EQ(mc_res_ok, reply.result())
        << "Expected result " << mc_res_to_string(mc_res_ok)
        << ", got " << mc_res_to_string(reply.result());

      CHECK_EQ(expectedVersion, reply.valueRangeSlow())
        << "Expected version " << expectedVersion
        << ", got " << reply.valueRangeSlow();

      --inflight_;
    });
}

void TestClient::waitForReplies(size_t remaining) {
  while (inflight_ > remaining) {
    loopOnce();
  }
  if (remaining == 0) {
    CHECK(pendingStat_ == 0) << "pendingStat_ should be 0";
    CHECK(inflightStat_ == 0) << "inflightStat_ should be 0";
  }
}

std::string genBigValue() {
  const size_t kBigValueSize = 1024 * 1024 * 16;
  std::string bigValue(kBigValueSize, '.');
  for (size_t i = 0; i < kBigValueSize; ++i) {
    bigValue[i] = 65 + (i % 26);
  }
  return bigValue;
}

}}} // facebook::memcache::test
