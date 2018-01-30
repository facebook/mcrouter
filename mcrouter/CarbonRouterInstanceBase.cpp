/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CarbonRouterInstanceBase.h"

#include <memory>

#include <boost/filesystem/operations.hpp>

#include <folly/Singleton.h>
#include <folly/system/ThreadName.h>

#include "mcrouter/AsyncWriter.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {

struct CarbonRouterInstanceBaseFunctionSchedulerTag {};
folly::Singleton<
    folly::FunctionScheduler,
    CarbonRouterInstanceBaseFunctionSchedulerTag>
    globalFunctionScheduler([]() {
      auto scheduler = std::make_unique<folly::FunctionScheduler>();
      scheduler->start();
      scheduler->setThreadName("carbon-global-scheduler");
      return scheduler.release();
    });

struct CarbonRouterLoggingAsyncWriter {};
folly::Singleton<AsyncWriter, CarbonRouterLoggingAsyncWriter>
    sharedLoggingAsyncWriter([]() {
      // Queue size starts at 1, we'll make it unlimited if requested.
      auto writer = std::make_unique<AsyncWriter>(1);
      if (!writer->start("mcrtr-statsw")) {
        throw std::runtime_error("Failed to spawn async stats logging thread");
      }
      return writer.release();
    });

struct CarbonRouterAsyncWriter {};
folly::Singleton<AsyncWriter, CarbonRouterAsyncWriter> sharedAsyncWriter([]() {
  auto writer = std::make_unique<AsyncWriter>();
  if (!writer->start("mcrtr-awriter")) {
    throw std::runtime_error("Failed to spawn mcrouter awriter thread");
  }
  return writer.release();
});

std::string statsUpdateFunctionName(folly::StringPiece routerName) {
  static std::atomic<uint64_t> uniqueId(0);
  return folly::to<std::string>(
      "carbon-stats-update-fn-", routerName, "-", uniqueId.fetch_add(1));
}

} // anonymous namespace

CarbonRouterInstanceBase::CarbonRouterInstanceBase(McrouterOptions inputOptions)
    : opts_(std::move(inputOptions)),
      pid_(getpid()),
      configApi_(createConfigApi(opts_)),
      rtVarsData_(std::make_shared<ObservableRuntimeVars>()),
      leaseTokenMap_(globalFunctionScheduler.try_get()),
      statsUpdateFunctionHandle_(statsUpdateFunctionName(opts_.router_name)) {
  if (auto statsLogger = statsLogWriter()) {
    if (opts_.stats_async_queue_length) {
      statsLogger->increaseMaxQueueSize(opts_.stats_async_queue_length);
    } else {
      statsLogger->makeQueueSizeUnlimited();
    }
  }
}

void CarbonRouterInstanceBase::setUpCompressionDictionaries(
    std::unordered_map<uint32_t, CodecConfigPtr>&& codecConfigs) noexcept {
  if (codecConfigs.empty() || compressionCodecManager_ != nullptr) {
    return;
  }
  compressionCodecManager_ = std::make_unique<const CompressionCodecManager>(
      std::move(codecConfigs));
}

void CarbonRouterInstanceBase::addStartupOpts(
    std::unordered_map<std::string, std::string> additionalOpts) {
  additionalStartupOpts_.insert(additionalOpts.begin(), additionalOpts.end());
}

std::unordered_map<std::string, std::string>
CarbonRouterInstanceBase::getStartupOpts() const {
  constexpr size_t kMaxOptionValueLength = 256;

  auto result = opts_.toDict();
  result.insert(additionalStartupOpts_.begin(), additionalStartupOpts_.end());
  result.emplace("version", MCROUTER_PACKAGE_STRING);
  for (auto& it : result) {
    it.second = shorten(it.second, kMaxOptionValueLength);
  }
  return result;
}

size_t CarbonRouterInstanceBase::nextProxyIndex() {
  std::lock_guard<std::mutex> guard(nextProxyMutex_);
  assert(nextProxy_ < opts().num_proxies);
  size_t res = nextProxy_;
  nextProxy_ = (nextProxy_ + 1) % opts().num_proxies;
  return res;
}

void CarbonRouterInstanceBase::registerForStatsUpdates() {
  if (!opts_.num_proxies) {
    return;
  }
  if (auto scheduler = functionScheduler()) {
    scheduler->addFunction(
        [this]() { updateStats(); },
        /*interval=*/std::chrono::seconds(MOVING_AVERAGE_BIN_SIZE_IN_SECOND),
        statsUpdateFunctionHandle_,
        /*startDelay=*/std::chrono::seconds(MOVING_AVERAGE_BIN_SIZE_IN_SECOND));
  }
}

void CarbonRouterInstanceBase::deregisterForStatsUpdates() {
  if (auto scheduler = functionScheduler()) {
    scheduler->cancelFunctionAndWait(statsUpdateFunctionHandle_);
  }
}

void CarbonRouterInstanceBase::updateStats() {
  const int BIN_NUM =
      (MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
       MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
  // To avoid inconsistence among proxies, we lock all mutexes together
  std::vector<std::unique_lock<std::mutex>> statsLocks;
  statsLocks.reserve(opts_.num_proxies);
  for (size_t i = 0; i < opts_.num_proxies; ++i) {
    statsLocks.push_back(getProxyBase(i)->stats().lock());
  }

  const auto idx = statsIndex();
  for (size_t i = 0; i < opts_.num_proxies; ++i) {
    auto* const proxy = getProxyBase(i);
    proxy->stats().aggregate(idx);
    proxy->advanceRequestStatsBin();
  }
  statsIndex((idx + 1) % BIN_NUM);
}

folly::ReadMostlySharedPtr<AsyncWriter>
CarbonRouterInstanceBase::statsLogWriter() {
  return sharedLoggingAsyncWriter.try_get_fast();
}

folly::ReadMostlySharedPtr<AsyncWriter>
CarbonRouterInstanceBase::asyncWriter() {
  return sharedAsyncWriter.try_get_fast();
}

std::shared_ptr<folly::FunctionScheduler>
CarbonRouterInstanceBase::functionScheduler() {
  return globalFunctionScheduler.try_get();
}

} // mcrouter
} // memcache
} // facebook
