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

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/Range.h>

#include "mcrouter/CallbackPool.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/LeaseTokenMap.h"
#include "mcrouter/McrouterClient.h"
#include "mcrouter/Observable.h"
#include "mcrouter/options.h"
#include "mcrouter/TkoTracker.h"

namespace facebook { namespace memcache { namespace mcrouter {

class AsyncWriter;
class McrouterManager;
class ProxyThread;
class RuntimeVarsData;
using ObservableRuntimeVars =
  Observable<std::shared_ptr<const RuntimeVarsData>>;

/**
 * A single mcrouter instance.  A mcrouter instance has a single config,
 * but might run across multiple threads.
 */
class McrouterInstance :
      public std::enable_shared_from_this<McrouterInstance> {
 public:
  /* Creation methods. All mcrouter instances are managed automatically,
     so the users don't need to worry about destruction. */

  /**
   * @return  If an instance with the given persistence_id already exists,
   *   returns a pointer to it. Options are ignored in this case.
   *   Otherwise spins up a new instance and returns the pointer to it.
   * @param evbs  Must be either empty or contain options.num_proxies
   *   event bases.  If empty, mcrouter will spawn its own proxy threads.
   *   Otherwise, proxies will run on the provided event bases
   *   (auxiliary threads will still be spawned).
   * @throw runtime_error  If no valid instance can be constructed from
   *   the provided options.
   */
  static McrouterInstance* init(
    folly::StringPiece persistence_id,
    const McrouterOptions& options,
    const std::vector<folly::EventBase*>& evbs = {});

  /**
   * If an instance with the given persistence_id already exists,
   * returns a pointer to it. Otherwise returns nullptr.
   */
  static McrouterInstance* get(folly::StringPiece persistence_id);

  /**
   * Intended for short-lived instances with unusual configs
   * (i.e. for debugging).
   *
   * Create a new mcrouter instance.
   * @return  Pointer to the newly brought up instance or nullptr
   *   if there was a problem starting it up.
   * @throw runtime_error  If no valid instance can be constructed from
   *   the provided options.
   */
  static std::shared_ptr<McrouterInstance> create(
    McrouterOptions input_options,
    const std::vector<folly::EventBase*>& evbs = {});

  /**
   * Create a handle to talk to mcrouter.
   * The context will be passed back to the callbacks.
   *
   * @param maximum_outstanding_requests  If nonzero, at most this many requests
   *   will be allowed to be in flight at any single point in time.
   *   send() will block until the number of outstanding requests
   *   is less than this limit.
   * @throw std::runtime_error  If the client cannot be created
   *   (i.e. attempting to create multiple clients to transient mcrouter).
   */
  McrouterClient::Pointer createClient(mcrouter_client_callbacks_t callbacks,
                                       void* client_context,
                                       size_t maximum_outstanding_requests);

  /**
   * Same as createClient(), but you must use it from the same thread that's
   * running the assigned proxy's event base.  The sends call into proxy
   * callbacks directly, bypassing the queue.
   */
  McrouterClient::Pointer createSameThreadClient(
    mcrouter_client_callbacks_t callbacks,
    void* client_context,
    size_t maximum_outstanding_requests);

  const McrouterOptions& opts() const {
    return opts_;
  }

  /**
   * Destroy all active instances.
   */
  static void freeAllMcrouters();

  std::unordered_map<std::string, std::string> getStartupOpts() const;

  void addStartupOpts(
    std::unordered_map<std::string, std::string> additionalOpts);

  /**
   * Shutdown all threads started by this instance. It's a blocking call and
   * should be called at most once. If it is not called, destructor will block
   * until all threads are stopped.
   */
  void shutdown() noexcept;

  /**
   * @return  nullptr if index is >= opts.num_proxies,
   *   pointer to the proxy otherwise.
   */
  proxy_t* getProxy(size_t index) const;

  /**
   * Release ownership of a proxy
   */
  proxy_t::Pointer releaseProxy(size_t index);

  pid_t pid() const {
    return pid_;
  }

  bool configure(folly::StringPiece input);

  ConfigApi& configApi() {
    assert(configApi_.get() != nullptr);
    return *configApi_;
  }

  uint64_t startTime() const {
    return startTime_;
  }

  time_t lastConfigAttempt() const {
    return lastConfigAttempt_;
  }

  size_t configFailures() const {
    return configFailures_;
  }

  TkoTrackerMap& tkoTrackerMap() {
    return tkoTrackerMap_;
  }

  /**
   * If lease pairing is enabled, return the lease token map.
   * Otherwise, return nullptr.
   */
  LeaseTokenMap* leaseTokenMap() {
    return leaseTokenMap_.get();
  }

  ObservableRuntimeVars& rtVarsData() {
    return rtVarsData_;
  }

  AsyncWriter& asyncWriter() {
    assert(asyncWriter_.get() != nullptr);
    return *asyncWriter_;
  }

  AsyncWriter& statsLogWriter() {
    assert(statsLogWriter_.get() != nullptr);
    return *statsLogWriter_;
  }

  McrouterInstance(const McrouterInstance&) = delete;
  McrouterInstance& operator=(const McrouterInstance&) = delete;
  McrouterInstance(McrouterInstance&&) noexcept = delete;
  McrouterInstance& operator=(McrouterInstance&&) = delete;

  const LogPostprocessCallbackFunc& postprocessCallback() const {
    return postprocessCallback_;
  }

  void setPostprocessCallback(LogPostprocessCallbackFunc&& newCallback) {
    postprocessCallback_ = std::move(newCallback);
  }

 private:
  const McrouterOptions opts_;

  pid_t pid_;

  std::mutex nextProxyMutex_;
  unsigned int nextProxy_{0};

  std::unique_ptr<ConfigApi> configApi_;
  CallbackPool<> onReconfigureSuccess_;

  // These next three fields are used for stats
  uint64_t startTime_{0};
  time_t lastConfigAttempt_{0};
  size_t configFailures_{0};

  // Lock to get before regenerating config structure
  std::mutex configReconfigLock_;

  LogPostprocessCallbackFunc postprocessCallback_;

  // Stat updater thread updates rate stat windows for each proxy
  std::thread statUpdaterThread_;

  std::mutex statUpdaterCvMutex_;
  std::condition_variable statUpdaterCv_;

  TkoTrackerMap tkoTrackerMap_;

  // Stores data for runtime variables.
  ObservableRuntimeVars rtVarsData_;

  /**
   * Logs mcrouter stats to disk every opts->stats_logging_interval
   * milliseconds
   */
  std::unique_ptr<McrouterLogger> mcrouterLogger_;

  /*
   * Asynchronous writer.
   */
  std::unique_ptr<AsyncWriter> asyncWriter_;

  std::unique_ptr<AsyncWriter> statsLogWriter_;

  std::atomic<bool> shutdownStarted_{false};

  // Auxiliary EventBase thread.
  folly::ScopedEventBaseThread evbAuxiliaryThread_;

  // Keep track of lease tokens of failed over requests.
  std::unique_ptr<LeaseTokenMap> leaseTokenMap_;

  ConfigApi::CallbackHandle configUpdateHandle_;

  std::unordered_map<std::string, std::string> additionalStartupOpts_;

  /**
   * Exactly one of these vectors will contain opts.num_proxies elements,
   * others will be empty.
   *
   * Standalone/sync mode: we don't startup proxy threads, so Mcrouter
   * owns the proxies directly.
   *
   * Embedded mode: Mcrouter owns ProxyThreads, which managed the lifetime
   * of proxies on their own threads.
   */
  std::vector<proxy_t::Pointer> proxies_;
  std::vector<std::unique_ptr<ProxyThread>> proxyThreads_;

  /**
   * The only reason this is a separate function is due to legacy accessor
   * needs.
   */
  static McrouterInstance* createRaw(
    McrouterOptions input_options,
    const std::vector<folly::EventBase*>& evbs);

  explicit McrouterInstance(McrouterOptions input_options);

  ~McrouterInstance();

  bool spinUp(const std::vector<folly::EventBase*>& evbs);

  void startAwriterThreads();
  void stopAwriterThreads() noexcept;

  void spawnAuxiliaryThreads();
  void joinAuxiliaryThreads() noexcept;
  void shutdownImpl() noexcept;

  void subscribeToConfigUpdate();

  void statUpdaterThreadRun();
  void spawnStatLoggerThread();
  void startObservingRuntimeVarsFile();

  /** (re)configure the router. true on success, false on error.
      NB file-based configuration is synchronous
      but server-based configuration is asynchronous */
  bool reconfigure();

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    static McrouterInstance* createRaw(
      const McrouterOptions& opts,
      const std::vector<folly::EventBase*>& evbs) {

      return McrouterInstance::createRaw(opts.clone(), evbs);
    }

    static void destroy(McrouterInstance* mcrouter) {
      delete mcrouter;
    }

    static CallbackPool<>&
    onReconfigureSuccess(McrouterInstance& mcrouter) {
      return mcrouter.onReconfigureSuccess_;
    }
  };

 private:
  friend class LegacyPrivateAccessor;
  friend class McrouterClient;
  friend class McrouterManager;
  friend class ProxyDestinationMap;
};

}}} // facebook::memcache::mcrouter
