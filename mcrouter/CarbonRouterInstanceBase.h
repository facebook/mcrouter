/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

#include <folly/Synchronized.h>
#include <folly/container/EvictingCacheMap.h>
#include <folly/experimental/FunctionScheduler.h>
#include <folly/experimental/ReadMostlySharedPtr.h>
#include <folly/fibers/TimedMutex.h>
#include <folly/io/async/EventBaseThread.h>
#include <folly/synchronization/CallOnce.h>

#include "mcrouter/ConfigApi.h"
#include "mcrouter/LeaseTokenMap.h"
#include "mcrouter/Observable.h"
#include "mcrouter/TkoTracker.h"
#include "mcrouter/options.h"

namespace facebook {
namespace memcache {

// Forward declarations
struct CodecConfig;
using CodecConfigPtr = std::unique_ptr<CodecConfig>;
class CompressionCodecManager;

namespace mcrouter {

// Forward declarations
class AsyncWriter;
template <class RouterInfo>
class Proxy;
class RuntimeVarsData;
using ObservableRuntimeVars =
    Observable<std::shared_ptr<const RuntimeVarsData>>;

using ShadowLeaseTokenMap = folly::Synchronized<
    folly::EvictingCacheMap<int64_t, int64_t>,
    folly::fibers::TimedMutex>;

class CarbonRouterInstanceBase {
 public:
  explicit CarbonRouterInstanceBase(McrouterOptions inputOptions);
  virtual ~CarbonRouterInstanceBase() = default;

  pid_t pid() const {
    return pid_;
  }

  const McrouterOptions& opts() const {
    return opts_;
  }

  /**
   * Returns compression codec manager.
   * If compression is disabled, this method will return nullptr.
   */
  const CompressionCodecManager* getCodecManager() const {
    return compressionCodecManager_.get();
  }

  void setUpCompressionDictionaries(
      std::unordered_map<uint32_t, CodecConfigPtr>&& codecConfigs) noexcept;

  TkoTrackerMap& tkoTrackerMap() {
    return tkoTrackerMap_;
  }

  ConfigApi& configApi() {
    assert(configApi_.get() != nullptr);
    return *configApi_;
  }

  ObservableRuntimeVars& rtVarsData() {
    return *rtVarsData_;
  }

  std::weak_ptr<ObservableRuntimeVars> rtVarsDataWeak() {
    return rtVarsData_;
  }

  /**
   * Returns an AsyncWriter for stats related purposes.
   */
  folly::ReadMostlySharedPtr<AsyncWriter> statsLogWriter();

  LeaseTokenMap& leaseTokenMap() {
    return leaseTokenMap_;
  }

  ShadowLeaseTokenMap& shadowLeaseTokenMap() {
    using UnsynchronizedMap = typename ShadowLeaseTokenMap::DataType;

    folly::call_once(shadowLeaseTokenMapInitFlag_, [this]() {
      shadowLeaseTokenMap_ = std::make_unique<ShadowLeaseTokenMap>(
          UnsynchronizedMap{opts().max_shadow_token_map_size});
    });
    return *shadowLeaseTokenMap_;
  }

  const LogPostprocessCallbackFunc& postprocessCallback() const {
    return postprocessCallback_;
  }

  void setPostprocessCallback(LogPostprocessCallbackFunc&& newCallback) {
    postprocessCallback_ = std::move(newCallback);
  }

  /**
   * Returns an AsyncWriter for mission critical work (use statsLogWriter() for
   * auxiliary / low priority work).
   */
  folly::ReadMostlySharedPtr<AsyncWriter> asyncWriter();

  std::unordered_map<std::string, std::string> getStartupOpts() const;
  void addStartupOpts(
      std::unordered_map<std::string, std::string> additionalOpts);

  uint64_t startTime() const {
    return startTime_;
  }

  time_t lastConfigAttempt() const {
    return lastConfigAttempt_;
  }

  size_t configFailures() const {
    return configFailures_;
  }

  bool configuredFromDisk() const {
    return configuredFromDisk_;
  }

  bool isRxmitReconnectionDisabled() const {
    return disableRxmitReconnection_;
  }

  /**
   * @return  nullptr if index is >= opts.num_proxies,
   *          pointer to the proxy otherwise.
   */
  virtual ProxyBase* getProxyBase(size_t index) const = 0;

  /**
   * Bump and return the index of the next proxy to be used by clients.
   */
  size_t nextProxyIndex();

  /**
   * Returns a FunctionScheduler suitable for running periodic background tasks
   * on. Null may be returned if the global instance has been destroyed.
   */
  std::shared_ptr<folly::FunctionScheduler> functionScheduler();

 protected:
  /**
   * Register this instance for periodic stats updates.
   */
  void registerForStatsUpdates();

  /**
   * Deregister this instance for periodic stats updates.
   */
  void deregisterForStatsUpdates();

  const McrouterOptions opts_;
  const pid_t pid_;
  const std::unique_ptr<ConfigApi> configApi_;

  LogPostprocessCallbackFunc postprocessCallback_;

  // These next four fields are used for stats
  uint64_t startTime_{0};
  time_t lastConfigAttempt_{0};
  size_t configFailures_{0};
  bool configuredFromDisk_{false};

  // Stores whether we should reconnect after hitting rxmit threshold
  std::atomic<bool> disableRxmitReconnection_{false};

  folly::Optional<folly::observer::Observer<std::string>> rtVarsDataObserver_;

 private:
  size_t statsIndex() const {
    return statsIndex_;
  }

  void statsIndex(size_t newIndex) {
    statsIndex_ = newIndex;
  }

  TkoTrackerMap tkoTrackerMap_;
  std::unique_ptr<const CompressionCodecManager> compressionCodecManager_;

  // Stores data for runtime variables.
  const std::shared_ptr<ObservableRuntimeVars> rtVarsData_;

  // Keep track of lease tokens of failed over requests.
  LeaseTokenMap leaseTokenMap_;

  // In order to shadow lease-sets properly, we need to pass the correct token
  // to the shadow destination.
  std::unique_ptr<ShadowLeaseTokenMap> shadowLeaseTokenMap_;
  folly::once_flag shadowLeaseTokenMapInitFlag_;

  std::unordered_map<std::string, std::string> additionalStartupOpts_;

  std::mutex nextProxyMutex_;
  size_t nextProxy_{0};

  // Current stats index. Only accessed / updated  by stats background thread.
  size_t statsIndex_{0};

  // Name of the stats update function registered with the function scheduler.
  const std::string statsUpdateFunctionHandle_;

  // Aggregates stats for all associated proxies. Should be called periodically.
  void updateStats();
};
}
}
} // facebook::memcache::mcrouter
