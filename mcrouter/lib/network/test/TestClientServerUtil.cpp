/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TestClientServerUtil.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/AsyncMcServer.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/lib/network/test/ListenSocket.h"

namespace folly {
class AsyncSocket;
} // namespace folly

namespace facebook {
namespace memcache {
namespace test {

const char* const kPemCaPath = "mcrouter/lib/network/test/ca_cert.pem";
const char* const kValidKeyPath = "mcrouter/lib/network/test/test_key.pem";
const char* const kValidCertPath = "mcrouter/lib/network/test/test_cert.pem";

const char* const kBrokenKeyPath = "mcrouter/lib/network/test/broken_key.pem";
const char* const kBrokenCertPath = "mcrouter/lib/network/test/broken_cert.pem";

const char* const kInvalidKeyPath = "/do/not/exist";
const char* const kInvalidCertPath = "/do/not/exist";

const char* const kServerVersion = "TestServer-1.0";

SSLContextProvider validClientSsl() {
  return []() {
    return getSSLContext(
        kValidCertPath, kValidKeyPath, kPemCaPath, folly::none, true);
  };
}

SSLContextProvider validSsl() {
  return
      []() { return getSSLContext(kValidCertPath, kValidKeyPath, kPemCaPath); };
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

TestServerOnRequest::TestServerOnRequest(
    folly::fibers::Baton& shutdownLock,
    bool outOfOrder)
    : shutdownLock_(shutdownLock), outOfOrder_(outOfOrder) {}

void TestServerOnRequest::onRequest(
    McServerRequestContext&& ctx,
    McGetRequest&& req) {
  using Reply = McGetReply;

  if (req.key().fullKey() == "sleep") {
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::seconds(1));
    processReply(std::move(ctx), Reply(mc_res_notfound));
  } else if (req.key().fullKey() == "shutdown") {
    shutdownLock_.post();
    processReply(std::move(ctx), Reply(mc_res_notfound));
    flushQueue();
  } else if (req.key().fullKey() == "busy") {
    processReply(std::move(ctx), Reply(mc_res_busy));
  } else {
    std::string value;
    if (req.key().fullKey().startsWith("value_size:")) {
      auto key = req.key().fullKey();
      key.removePrefix("value_size:");
      size_t valSize = folly::to<size_t>(key);
      value = std::string(valSize, 'a');
    } else if (req.key().fullKey() == "trace_id") {
      const auto traceMetadata = req.traceToInts();
      value =
          folly::sformat("{}:{}", traceMetadata.first, traceMetadata.second);
    } else if (req.key().fullKey() != "empty") {
      value = req.key().fullKey().str();
    }

    Reply foundReply(mc_res_found);
    foundReply.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, value);

    if (req.key().fullKey() == "hold") {
      waitingReplies_.push_back(
          [ ctx = std::move(ctx), reply = std::move(foundReply) ]() mutable {
            McServerRequestContext::reply(std::move(ctx), std::move(reply));
          });
    } else if (req.key().fullKey() == "flush") {
      processReply(std::move(ctx), std::move(foundReply));
      flushQueue();
    } else {
      processReply(std::move(ctx), std::move(foundReply));
    }
  }
}

void TestServerOnRequest::onRequest(
    McServerRequestContext&& ctx,
    McSetRequest&&) {
  processReply(std::move(ctx), McSetReply(mc_res_stored));
}

void TestServerOnRequest::onRequest(
    McServerRequestContext&& ctx,
    McVersionRequest&&) {
  McVersionReply reply(mc_res_ok);
  reply.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, kServerVersion);
  processReply(std::move(ctx), std::move(reply));
}

void TestServerOnRequest::flushQueue() {
  for (size_t i = 0; i < waitingReplies_.size(); ++i) {
    waitingReplies_[i]();
  }
  waitingReplies_.clear();
}

TestServer::TestServer(
    bool outOfOrder,
    bool useSsl,
    int maxInflight,
    int timeoutMs,
    size_t maxConns,
    bool useDefaultVersion,
    size_t numThreads,
    bool useTicketKeySeeds,
    size_t goAwayTimeoutMs,
    bool tfoEnabled)
    : outOfOrder_(outOfOrder), useTicketKeySeeds_(useSsl && useTicketKeySeeds) {
  opts_.existingSocketFd = sock_.getSocketFd();
  opts_.numThreads = numThreads;
  opts_.worker.defaultVersionHandler = useDefaultVersion;
  opts_.worker.maxInFlight = maxInflight;
  opts_.worker.sendTimeout = std::chrono::milliseconds{timeoutMs};
  opts_.worker.goAwayTimeout = std::chrono::milliseconds{goAwayTimeoutMs};
  opts_.setPerThreadMaxConns(maxConns, opts_.numThreads);
  if (useSsl) {
    opts_.pemKeyPath = kValidKeyPath;
    opts_.pemCertPath = kValidCertPath;
    opts_.pemCaPath = kPemCaPath;
    if (tfoEnabled) {
      opts_.tfoEnabledForSsl = true;
      opts_.tfoQueueSize = 100000;
    }
  }
}

void TestServer::run(std::function<void(AsyncMcServerWorker&)> init) {
  LOG(INFO) << "Spawning AsyncMcServer";

  folly::fibers::Baton startupLock;
  serverThread_ = std::thread([this, &startupLock, init] {
    // take ownership of the socket before starting the server as
    // its closed in the server
    sock_.releaseSocketFd();

    server_ = std::make_unique<AsyncMcServer>(opts_);
    if (useTicketKeySeeds_) {
      wangle::TLSTicketKeySeeds seeds{
          .oldSeeds = {"aaaa"}, .currentSeeds = {"bbbb"}, .newSeeds = {"cccc"},
      };
      server_->setTicketKeySeeds(std::move(seeds));
    }
    server_->spawn([this, init](
        size_t, folly::EventBase& evb, AsyncMcServerWorker& worker) {
      init(worker);
      worker.setOnConnectionAccepted([this]() { ++acceptedConns_; });

      evb.loop();
    });

    // allow server some time to startup
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    startupLock.post();

    shutdownLock_.wait();
    server_->shutdown();
    server_->join();
  });
  startupLock.wait();
}

std::string TestServer::version() const {
  if (opts_.worker.defaultVersionHandler) {
    return opts_.worker.versionString;
  } else {
    return kServerVersion;
  }
}

TestClient::TestClient(
    std::string host,
    uint16_t port,
    int timeoutMs,
    mc_protocol_t protocol,
    SSLContextProvider ssl,
    uint64_t qosClass,
    uint64_t qosPath,
    std::string serviceIdentity,
    const CompressionCodecMap* compressionCodecMap,
    bool enableTfo)
    : fm_(std::make_unique<folly::fibers::EventBaseLoopController>()) {
  dynamic_cast<folly::fibers::EventBaseLoopController&>(fm_.loopController())
      .attachEventBase(eventBase_);
  ConnectionOptions opts(host, port, protocol);
  opts.writeTimeout = std::chrono::milliseconds(timeoutMs);
  opts.compressionCodecMap = compressionCodecMap;
  if (ssl) {
    opts.sslContextProvider = std::move(ssl);
    opts.sessionCachingEnabled = true;
    opts.sslServiceIdentity = serviceIdentity;
    opts.tfoEnabledForSsl = enableTfo;
  }
  if (qosClass != 0 || qosPath != 0) {
    opts.enableQoS = true;
    opts.qosClass = qosClass;
    opts.qosPath = qosPath;
  }
  client_ = std::make_unique<AsyncMcClient>(eventBase_, opts);
  client_->setStatusCallbacks(
      [](const folly::AsyncSocket&) { LOG(INFO) << "Client UP."; },
      [](AsyncMcClient::ConnectionDownReason reason) {
        if (reason == AsyncMcClient::ConnectionDownReason::SERVER_GONE_AWAY) {
          LOG(INFO) << "Server gone Away.";
        } else {
          LOG(INFO) << "Client DOWN.";
        }
      });
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

void TestClient::setStatusCallbacks(
    std::function<void(const folly::AsyncSocket&)> onUp,
    std::function<void(AsyncMcClient::ConnectionDownReason)> onDown) {
  client_->setStatusCallbacks(
      [onUp](const folly::AsyncSocket& socket) {
        LOG(INFO) << "Client UP.";
        onUp(socket);
      },
      [onDown](AsyncMcClient::ConnectionDownReason reason) {
        if (reason == AsyncMcClient::ConnectionDownReason::SERVER_GONE_AWAY) {
          LOG(INFO) << "Server gone Away.";
        } else {
          LOG(INFO) << "Client DOWN.";
        }
        onDown(reason);
      });
}

void TestClient::sendGet(
    std::string key,
    mc_res_t expectedResult,
    uint32_t timeoutMs,
    std::function<void(const ReplyStatsContext&)> replyStatsCallback) {
  inflight_++;
  fm_.addTask([
    key = std::move(key),
    expectedResult,
    replyStatsCallback = std::move(replyStatsCallback),
    this,
    timeoutMs
  ]() {
    McGetRequest req(key);
    if (req.key().fullKey() == "trace_id") {
      req.setTraceId({12345, 67890});
    }

    try {
      ReplyStatsContext replyStatsContext;
      auto reply = client_->sendSync(
          req, std::chrono::milliseconds(timeoutMs), &replyStatsContext);
      if (replyStatsCallback) {
        replyStatsCallback(replyStatsContext);
      }

      if (reply.result() == mc_res_found) {
        auto value = carbon::valueRangeSlow(reply);
        if (req.key().fullKey() == "empty") {
          checkLogic(value.empty(), "Expected empty value, got {}", value);
        } else if (req.key().fullKey().startsWith("value_size:")) {
          auto key = req.key().fullKey();
          key.removePrefix("value_size:");
          size_t valSize = folly::to<size_t>(key);
          checkLogic(
              value.size() == valSize,
              "Expected value of size {}, got {}",
              valSize,
              value.size());
        } else if (req.key().fullKey() == "trace_id") {
          checkLogic(
              value == "12345:67890",
              "Expected value to equal trace ID {}, got {}",
              "12345:67890",
              value);
        } else {
          checkLogic(
              value == req.key().fullKey(),
              "Expected {}, got {}",
              req.key().fullKey(),
              value);
        }
      }
      checkLogic(
          expectedResult == reply.result(),
          "Expected {}, got {} for key '{}'",
          mc_res_to_string(expectedResult),
          mc_res_to_string(reply.result()),
          req.key().fullKey());
    } catch (const std::exception& e) {
      CHECK(false) << "Failed: " << e.what();
    }
    inflight_--;
  });
}

void TestClient::sendSet(
    std::string key,
    std::string value,
    mc_res_t expectedResult,
    std::function<void(const ReplyStatsContext&)> replyStatsCallback) {
  inflight_++;
  fm_.addTask([
    key = std::move(key),
    value = std::move(value),
    expectedResult,
    replyStatsCallback = std::move(replyStatsCallback),
    this
  ]() {
    McSetRequest req(key);
    req.value() = folly::IOBuf::wrapBufferAsValue(folly::StringPiece(value));

    ReplyStatsContext replyStatsContext;
    auto reply = client_->sendSync(
        req, std::chrono::milliseconds(200), &replyStatsContext);
    if (replyStatsCallback) {
      replyStatsCallback(replyStatsContext);
    }

    CHECK(expectedResult == reply.result())
        << "Expected: " << mc_res_to_string(expectedResult) << " got "
        << mc_res_to_string(reply.result());

    inflight_--;
  });
}

void TestClient::sendVersion(std::string expectedVersion) {
  ++inflight_;
  fm_.addTask([ this, expectedVersion = std::move(expectedVersion) ]() {
    McVersionRequest req;

    auto reply = client_->sendSync(req, std::chrono::milliseconds(200));

    CHECK_EQ(mc_res_ok, reply.result())
        << "Expected result " << mc_res_to_string(mc_res_ok) << ", got "
        << mc_res_to_string(reply.result());

    CHECK_EQ(expectedVersion, carbon::valueRangeSlow(reply))
        << "Expected version " << expectedVersion << ", got "
        << carbon::valueRangeSlow(reply);

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
} // test
} // memcache
} // facebook
