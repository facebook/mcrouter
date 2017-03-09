/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include <folly/io/async/EventBaseThread.h>

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

  AsyncWriter& statsLogWriter() {
    assert(statsLogWriter_.get() != nullptr);
    return *statsLogWriter_;
  }

  LeaseTokenMap& leaseTokenMap() {
    return *leaseTokenMap_;
  }

  const LogPostprocessCallbackFunc& postprocessCallback() const {
    return postprocessCallback_;
  }

  void setPostprocessCallback(LogPostprocessCallbackFunc&& newCallback) {
    postprocessCallback_ = std::move(newCallback);
  }

  AsyncWriter& asyncWriter() {
    assert(asyncWriter_.get() != nullptr);
    return *asyncWriter_;
  }

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

  size_t configsFromDisk() const {
    return configsFromDisk_;
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

 protected:
  const McrouterOptions opts_;
  const pid_t pid_;
  const std::unique_ptr<ConfigApi> configApi_;

  const std::unique_ptr<AsyncWriter> statsLogWriter_;

  /*
   * Asynchronous writer.
   */
  const std::unique_ptr<AsyncWriter> asyncWriter_;

  // Auxiliary EventBase thread.
  folly::EventBaseThread evbAuxiliaryThread_;

  LogPostprocessCallbackFunc postprocessCallback_;

  // These next four fields are used for stats
  uint64_t startTime_{0};
  time_t lastConfigAttempt_{0};
  size_t configFailures_{0};
  size_t configsFromDisk_{0};

  // Stores whether we should reconnect after hitting rxmit threshold
  std::atomic<bool> disableRxmitReconnection_{false};

  folly::Optional<folly::observer::Observer<std::string>> rtVarsDataObserver_;

 private:
  TkoTrackerMap tkoTrackerMap_;
  std::unique_ptr<const CompressionCodecManager> compressionCodecManager_;

  // Stores data for runtime variables.
  const std::shared_ptr<ObservableRuntimeVars> rtVarsData_;

  // Keep track of lease tokens of failed over requests.
  const std::unique_ptr<LeaseTokenMap> leaseTokenMap_;

  std::unordered_map<std::string, std::string> additionalStartupOpts_;

  std::mutex nextProxyMutex_;
  size_t nextProxy_{0};
};
}
}
} // facebook::memcache::mcrouter
