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
#include <mutex>
#include <unordered_map>

#include <folly/Range.h>

#include "mcrouter/CallbackPool.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/fbi/cpp/ShutdownLock.h"
#include "mcrouter/lib/fbi/cpp/StartupLock.h"
#include "mcrouter/McrouterClient.h"
#include "mcrouter/Observable.h"
#include "mcrouter/options.h"
#include "mcrouter/PeriodicTaskScheduler.h"
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
class McrouterInstance {
 public:
  /* Creation methods. All mcrouter instances are managed automatically,
     so the users don't need to worry about destruction. */

  /**
   * @return  If an instance with the given persistence_id already exists,
   *   returns a pointer to it. Options are ignored in this case.
   *   Otherwise spins up a new instance and returns the pointer to it.
   * @throw runtime_error  If no valid instance can be constructed from
   *   the provided options.
   */
  static McrouterInstance* init(folly::StringPiece persistence_id,
                                const McrouterOptions& options);

  /**
   * If an instance with the given persistence_id already exists,
   * returns a pointer to it. Otherwise returns nullptr.
   */
  static McrouterInstance* get(folly::StringPiece persistence_id);

  /**
   * Intended for short-lived instances with unusual configs
   * (i.e. for debugging).
   *
   * @return  Pointer to the newly brought up instance or nullptr
   *   if there was a problem starting it up.  Only one client
   *   is allowed to be created to the instance.  On destruction of that client,
   *   the instance is destroyed as well.
   */
  static McrouterInstance* createTransient(const McrouterOptions& options);

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

  std::string routerName() const;

  bool shutdownStarted() {
    return shutdownLock_.shutdownStarted();
  }

  ShutdownLock& shutdownLock() {
    return shutdownLock_;
  }

  /**
   * @return  True if we want to run with realtime threads, that is if we're
   *   running as both standalone and with realtime requested.
   */
  bool wantRealtimeThreads() const {
    return opts_.standalone && !opts_.realtime_disabled;
  }

  /**
   * @return  nullptr if index is >= opts.num_proxies,
   *   pointer to the proxy otherwise.
   */
  proxy_t* getProxy(size_t index) const;

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

  ObservableRuntimeVars& rtVarsData() {
    return rtVarsData_;
  }

  StartupLock& startupLock() {
    return startupLock_;
  }

  AsyncWriter& asyncWriter() {
    assert(asyncWriter_.get() != nullptr);
    return *asyncWriter_;
  }

  AsyncWriter& statsLogWriter() {
    assert(statsLogWriter_.get() != nullptr);
    return *statsLogWriter_;
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

  // Stat updater thread updates rate stat windows for each proxy
  pthread_t statUpdaterThreadHandle_{0};
  void* statUpdaterThreadStack_{nullptr};

  std::mutex statUpdaterCvMutex_;
  std::condition_variable statUpdaterCv_;

  std::mutex clientListLock_;

  McrouterClient::Queue clientList_;

  // If true, allow only one mcrouter client and shutdown mcrouter
  // after it disconnects
  bool isTransient_{false};

  // For a transient mcrouter, number of alive clients
  // (different from the size of client_list,
  // as a disconnected client stays alive until all the requests come back).
  //
  // Needs to be thread safe, as we create clients on the client thread
  // and destroy them on a proxy thread.
  //
  // Currently limited to one.
  std::atomic<int> liveClients_{0};

  TkoTrackerMap tkoTrackerMap_;

  // Stores data for runtime variables.
  ObservableRuntimeVars rtVarsData_;

  StartupLock startupLock_;

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

  std::function<void(size_t, proxy_t*)> onDestroyProxy_;

  ShutdownLock shutdownLock_;

  // Used to shedule periodic tasks for mcrouter.
  PeriodicTaskScheduler taskScheduler_;

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
  std::vector<std::unique_ptr<proxy_t>> proxies_;
  std::vector<std::unique_ptr<ProxyThread>> proxyThreads_;

  /**
   * Create a new mcrouter instance.
   * @return  Pointer to the newly brought up instance or nullptr
   *   if there was a problem starting it up.
   * @throw runtime_error  If no valid instance can be constructed from
   *   the provided options.
   */
  static McrouterInstance* create(const McrouterOptions& input_options,
                                  bool spawnProxyThreads = true);

  explicit McrouterInstance(McrouterOptions input_options);

  ~McrouterInstance();

  bool spinUp(bool spawnProxyThreads);
  void tearDown();

  void startAwriterThreads();
  void stopAwriterThreads();

  void spawnAuxiliaryThreads();
  void shutdownAndJoinAuxiliaryThreads();
  void joinAuxiliaryThreads();

  void subscribeToConfigUpdate();

  static void* statUpdaterThreadRun(void* arg);
  void spawnStatUpdaterThread();
  void spawnStatLoggerThread();
  void startObservingRuntimeVarsFile();
  void onClientDestroyed();

  /** (re)configure the router. true on success, false on error.
      NB file-based configuration is synchronous
      but server-based configuration is asynchronous */
  bool reconfigure();

  McrouterInstance(const McrouterInstance&) = delete;
  McrouterInstance& operator=(const McrouterInstance&) = delete;
  McrouterInstance(McrouterInstance&&) noexcept = delete;
  McrouterInstance& operator=(McrouterInstance&&) = delete;

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    static McrouterInstance* create(const McrouterOptions& opts,
                                    bool spawnProxyThreads) {
      return McrouterInstance::create(opts, spawnProxyThreads);
    }

    static void tearDown(McrouterInstance& mcrouter) {
      mcrouter.tearDown();
    }

    static CallbackPool<>&
    onReconfigureSuccess(McrouterInstance& mcrouter) {
      return mcrouter.onReconfigureSuccess_;
    }

    static std::function<void(size_t, proxy_t*)>&
    onDestroyProxy(McrouterInstance& mcrouter) {
      return mcrouter.onDestroyProxy_;
    }
  };

 private:
  friend class LegacyPrivateAccessor;
  friend class McrouterClient;
  friend class McrouterManager;
  friend class ProxyDestinationMap;
};

}}} // facebook::memcache::mcrouter
