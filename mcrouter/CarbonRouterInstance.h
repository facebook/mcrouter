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
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <folly/Range.h>

#include "mcrouter/CallbackPool.h"
#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/FileObserver.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyConfigBuilder.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class McrouterManager;

class ProxyThread;

/**
 * A single mcrouter instance.  A mcrouter instance has a single config,
 * but might run across multiple threads.
 */
template <class RouterInfo>
class CarbonRouterInstance
    : public CarbonRouterInstanceBase,
      public std::enable_shared_from_this<CarbonRouterInstance<RouterInfo>> {
 public:
  /* Creation methods. All mcrouter instances are managed automatically,
     so the users don't need to worry about destruction. */

  /**
   * @return  If an instance with the given persistence_id already exists,
   *   returns a pointer to it. Options are ignored in this case.
   *   Otherwise spins up a new instance and returns the pointer to it. May
   *   return nullptr if the McRouterManager singleton is unavailable, perhaps
   *   due to misconfiguration.
   * @param evbs  Must be either empty or contain options.num_proxies
   *   event bases.  If empty, mcrouter will spawn its own proxy threads.
   *   Otherwise, proxies will run on the provided event bases
   *   (auxiliary threads will still be spawned).
   * @throw runtime_error  If no valid instance can be constructed from
   *   the provided options.
   */
  static CarbonRouterInstance<RouterInfo>* init(
      folly::StringPiece persistence_id,
      const McrouterOptions& options,
      const std::vector<folly::EventBase*>& evbs =
          std::vector<folly::EventBase*>());

  /**
   * If an instance with the given persistence_id already exists,
   * returns a pointer to it. Otherwise returns nullptr.
   */
  static CarbonRouterInstance<RouterInfo>* get(
      folly::StringPiece persistence_id);

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
  static std::shared_ptr<CarbonRouterInstance<RouterInfo>> create(
      McrouterOptions input_options,
      const std::vector<folly::EventBase*>& evbs = {});

  /**
   * Destroys ALL active instances for ALL RouterInfos.
   */
  static void freeAllMcrouters();

  /**
   * Create a handle to talk to mcrouter.
   *
   * @param maximum_outstanding_requests  If nonzero, at most this many requests
   *   will be allowed to be in flight at any single point in time.
   *   send() will block until the number of outstanding requests
   *   is less than this limit.
   * @throw std::runtime_error  If the client cannot be created
   *   (i.e. attempting to create multiple clients to transient mcrouter).
   */
  typename CarbonRouterClient<RouterInfo>::Pointer createClient(
      size_t maximum_outstanding_requests,
      bool maximum_outstanding_requests_error = false);

  /**
   * Same as createClient(), but you must use it from the same thread that's
   * running the assigned proxy's event base.  The sends call into proxy
   * callbacks directly, bypassing the queue.
   */
  typename CarbonRouterClient<RouterInfo>::Pointer createSameThreadClient(
      size_t maximum_outstanding_requests);

  /**
   * Shutdown all threads started by this instance. It's a blocking call and
   * should be called at most once. If it is not called, destructor will block
   * until all threads are stopped.
   */
  void shutdown() noexcept;

  ProxyBase* getProxyBase(size_t index) const override;

  /**
   * @return  nullptr if index is >= opts.num_proxies,
   *   pointer to the proxy otherwise.
   */
  Proxy<RouterInfo>* getProxy(size_t index) const;

  CarbonRouterInstance(const CarbonRouterInstance&) = delete;
  CarbonRouterInstance& operator=(const CarbonRouterInstance&) = delete;
  CarbonRouterInstance(CarbonRouterInstance&&) noexcept = delete;
  CarbonRouterInstance& operator=(CarbonRouterInstance&&) = delete;

 private:
  CallbackPool<> onReconfigureSuccess_;

  // Lock to get before regenerating config structure
  std::mutex configReconfigLock_;

  // Corresponding handle
  ObservableRuntimeVars::CallbackHandle rxmitHandle_;

  /**
   * Logs mcrouter stats to disk every opts->stats_logging_interval
   * milliseconds
   */
  std::unique_ptr<McrouterLogger> mcrouterLogger_;

  std::atomic<bool> shutdownStarted_{false};

  FileObserverHandle runtimeVarsObserverHandle_;

  ConfigApi::CallbackHandle configUpdateHandle_;

  /**
   * Both these vectors will contain opts.num_proxies elements.
   */
  std::vector<Proxy<RouterInfo>*> proxies_;
  std::vector<std::unique_ptr<folly::VirtualEventBase>> proxyEvbs_;

  /**
   * This will contain opts.num_proxies elements in Embedded mode (mcrouter
   * owns proxy threads).
   * In case of Standalone/sync mode, this vector is empty.
   */
  std::vector<std::unique_ptr<ProxyThread>> proxyThreads_;

  /**
   * The only reason this is a separate function is due to legacy accessor
   * needs.
   */
  static CarbonRouterInstance<RouterInfo>* createRaw(
      McrouterOptions input_options,
      const std::vector<folly::EventBase*>& evbs);

  explicit CarbonRouterInstance(McrouterOptions input_options);

  ~CarbonRouterInstance() override;

  folly::Expected<folly::Unit, std::string> spinUp(
      const std::vector<folly::EventBase*>& evbs);

  void spawnAuxiliaryThreads();
  void joinAuxiliaryThreads() noexcept;
  void shutdownImpl() noexcept;

  void subscribeToConfigUpdate();

  void spawnStatLoggerThread();
  void startObservingRuntimeVarsFile();

  folly::Expected<folly::Unit, std::string> configure(
      const ProxyConfigBuilder& builder);
  /** (re)configure the router. true on success, false on error.
      NB file-based configuration is synchronous
      but server-based configuration is asynchronous */
  bool reconfigure(const ProxyConfigBuilder& builder);
  /** Create the ProxyConfigBuilder used to reconfigure.
  Returns error reason if constructor fails. **/
  folly::Expected<ProxyConfigBuilder, std::string> createConfigBuilder();

  void registerOnUpdateCallbackForRxmits();

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    static CarbonRouterInstance<RouterInfo>* createRaw(
        const McrouterOptions& opts,
        const std::vector<folly::EventBase*>& evbs) {
      return CarbonRouterInstance<RouterInfo>::createRaw(opts.clone(), evbs);
    }

    static void destroy(CarbonRouterInstance<RouterInfo>* mcrouter) {
      delete mcrouter;
    }

    static CallbackPool<>& onReconfigureSuccess(
        CarbonRouterInstance<RouterInfo>& mcrouter) {
      return mcrouter.onReconfigureSuccess_;
    }
  };

 private:
  friend class LegacyPrivateAccessor;
  friend class CarbonRouterClient<RouterInfo>;
  friend class McrouterManager;
  friend class ProxyDestinationMap;
};

/**
 * Destroy all active instances.
 */
void freeAllRouters();

} // mcrouter
} // memcache
} // facebook

#include "CarbonRouterInstance-inl.h"
