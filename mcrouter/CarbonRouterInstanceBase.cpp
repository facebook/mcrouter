/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include <folly/Indestructible.h>
#include <folly/Singleton.h>
#include <folly/ThreadName.h>

#include "mcrouter/AsyncWriter.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {

// Mutex protecting statsUpdateRegisteredInstances.
folly::Indestructible<std::mutex> statsUpdateLock;
// Condition variable used to notify the stats background thread. Used with
// statsUpdateLock.
folly::Indestructible<std::condition_variable> statsUpdateCv;
// The set of instances registered for stats updates. Protected by
// statsUpdateLock.
folly::Indestructible<std::unordered_set<CarbonRouterInstanceBase*>>
    statsUpdateRegisteredInstances;
// Mutex protecting starting and stopping of statsUpdateThread.
folly::Indestructible<std::mutex> statsUpdateThreadControlLock;
// Background thread for stats updates . Protected by
// statsUpdateThreadControlLock.
folly::Indestructible<std::thread> statsUpdateThread;

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

} // namespace

CarbonRouterInstanceBase::CarbonRouterInstanceBase(McrouterOptions inputOptions)
    : opts_(std::move(inputOptions)),
      pid_(getpid()),
      configApi_(createConfigApi(opts_)),
      asyncWriter_(std::make_unique<AsyncWriter>()),
      rtVarsData_(std::make_shared<ObservableRuntimeVars>()),
      leaseTokenMap_(std::make_unique<LeaseTokenMap>(evbAuxiliaryThread_)) {
  evbAuxiliaryThread_.getEventBase()->runInEventBaseThread(
      [] { folly::setThreadName("CarbonAux"); });
  if (auto statsLogger = statsLogWriter()) {
    if (opts_.stats_async_queue_length) {
      statsLogger->increaseMaxQueueSize(opts_.stats_async_queue_length);
    } else {
      statsLogger->makeQueueSizeUnlimited();
    }
  }
}

CarbonRouterInstanceBase::~CarbonRouterInstanceBase() {
  // Complete all outstanding stats logging tasks to ensure all tasks related
  // to this instance are finished.
  if (auto statsLogger = statsLogWriter()) {
    statsLogger->completePendingTasks();
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
  std::lock_guard<std::mutex> controlLock(*statsUpdateThreadControlLock);
  std::lock_guard<std::mutex> updateLock(*statsUpdateLock);
  statsUpdateRegisteredInstances->insert(this);
  // Start the background thread if needed.
  if (!statsUpdateThread->joinable()) {
    *statsUpdateThread = std::thread(&statUpdaterThreadRun);
  }
}

void CarbonRouterInstanceBase::deregisterForStatsUpdates() {
  std::lock_guard<std::mutex> controlLock(*statsUpdateThreadControlLock);
  std::unique_lock<std::mutex> updateLock(*statsUpdateLock);
  statsUpdateRegisteredInstances->erase(this);
  // TODO(bwatling): determine if we are actually forking anywhere.
  if (getpid() != pid_) {
    LOG(WARNING) << "getpid() != pid_, not joining stats update thread";
    return;
  }
  // Join the background thread if there are no instances registered.
  if (statsUpdateRegisteredInstances->empty() &&
      statsUpdateThread->joinable()) {
    updateLock.unlock();
    statsUpdateCv->notify_all();
    statsUpdateThread->join();
  }
}

void CarbonRouterInstanceBase::statUpdaterThreadRun() {
  folly::setThreadName("mcrtr-stats");
  const int BIN_NUM =
      (MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
       MOVING_AVERAGE_BIN_SIZE_IN_SECOND);

  std::unique_lock<std::mutex> lock(*statsUpdateLock);
  while (!statsUpdateRegisteredInstances->empty()) {
    statsUpdateCv->wait_for(
        lock, std::chrono::seconds(MOVING_AVERAGE_BIN_SIZE_IN_SECOND));
    for (auto* const instance : *statsUpdateRegisteredInstances) {
      // To avoid inconsistence among proxies, we lock all mutexes together
      std::vector<std::unique_lock<std::mutex>> statsLocks;
      statsLocks.reserve(instance->opts_.num_proxies);
      for (size_t i = 0; i < instance->opts_.num_proxies; ++i) {
        statsLocks.push_back(instance->getProxyBase(i)->stats().lock());
      }

      const auto idx = instance->statsIndex();
      for (size_t i = 0; i < instance->opts_.num_proxies; ++i) {
        auto* const proxy = instance->getProxyBase(i);
        proxy->stats().aggregate(idx);
        proxy->advanceRequestStatsBin();
      }
      instance->statsIndex((idx + 1) % BIN_NUM);
    }
  }
}

folly::ReadMostlySharedPtr<AsyncWriter>
CarbonRouterInstanceBase::statsLogWriter() {
  return sharedLoggingAsyncWriter.try_get_fast();
}

} // mcrouter
} // memcache
} // facebook
