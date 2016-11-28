/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <dirent.h>
#include <event.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <atomic>
#include <memory>
#include <random>
#include <string>

#include <folly/detail/CacheLocality.h>
#include <folly/fibers/FiberManager.h>
#include <folly/Range.h>

#include "mcrouter/AsyncLog.h"
#include "mcrouter/config.h"
#include "mcrouter/CyclesObserver.h"
#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"
#include "mcrouter/Observable.h"
#include "mcrouter/options.h"
#include "mcrouter/ProxyRequestPriority.h"
#include "mcrouter/ProxyStats.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
  struct dynamic;
  class File;
} // folly

namespace facebook { namespace memcache {

template <int op_id>
struct McOperation;
template <class T>
class MessageQueue;

namespace mcrouter {
// forward declaration
class McrouterClient;
class McrouterInstance;
class McrouterInstanceBase;
template <class RouteHandleIf>
class ProxyConfig;
class ProxyDestination;
class ProxyDestinationMap;
class ProxyRequestContext;
template <class RouteHandleIf, class Request>
class ProxyRequestContextTyped;
class RuntimeVarsData;
class ShardSplitter;

using ObservableRuntimeVars =
    Observable<std::shared_ptr<const RuntimeVarsData>>;

using McrouterProxyConfig = ProxyConfig<McrouterRouteHandleIf>;

struct ShadowSettings {
  /**
   * @return  nullptr if config is invalid, new ShadowSettings struct otherwise
   */
  static std::shared_ptr<ShadowSettings>
  create(const folly::dynamic& json, McrouterInstanceBase& router);

  ~ShadowSettings();

  const std::string& keyFractionRangeRv() const {
    return keyFractionRangeRv_;
  }

  size_t startIndex() const {
    return startIndex_;
  }

  size_t endIndex() const {
    return endIndex_;
  }

  bool validateRepliesFlag() const {
    return validateReplies_;
  }

  // [start, end] where 0 <= start <= end <= numeric_limits<uint32_t>::max()
  std::pair<uint32_t, uint32_t> keyRange() const {
    auto fraction = keyRange_.load();
    return { fraction >> 32, fraction & ((1UL << 32) - 1) };
  }

  /**
   * @throws std::logic_error if !(0 <= start <= end <= 1)
   */
  void setKeyRange(double start, double end);

 private:
  ObservableRuntimeVars::CallbackHandle handle_;
  void registerOnUpdateCallback(McrouterInstanceBase& router);

  std::string keyFractionRangeRv_;
  size_t startIndex_{0};
  size_t endIndex_{0};

  std::atomic<uint64_t> keyRange_{0};

  bool validateReplies_{false};

  ShadowSettings() = default;
};

struct ProxyMessage {
  enum class Type {
    REQUEST,
    OLD_CONFIG,
    SHUTDOWN
  };

  Type type{Type::REQUEST};
  void* data{nullptr};

  constexpr ProxyMessage() = default;

  ProxyMessage(Type t, void* d) noexcept
      : type(t), data(d) {}
};

class Proxy {
 public:
  folly::EventBase& eventBase() const {
    assert(eventBase_ != nullptr);
    return *eventBase_;
  }

  ~Proxy();

  /**
   * Access to config - can only be called on the proxy thread
   * and the resulting shared_ptr can only be detroyed on the proxy thread.
   */
  std::shared_ptr<McrouterProxyConfig> getConfigUnsafe() const;

  /**
   * Can be called from any thread.
   *
   * Returns a lock and a reference to the config.
   * The caller may only access the config through the reference
   * while the lock is held.
   */
  std::pair<std::unique_lock<SFRReadLock>, McrouterProxyConfig&>
  getConfigLocked() const;

  /**
   * Thread-safe config swap; returns the previous contents of
   * the config pointer
   */
  std::shared_ptr<McrouterProxyConfig> swapConfig(
      std::shared_ptr<McrouterProxyConfig> newConfig);

  /** Queue up and route the new incoming request */
  template <class RouteHandleIf, class Request>
  void dispatchRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<RouteHandleIf, Request>> ctx);

  /**
   * Put a new proxy message into the queue.
   */
  void sendMessage(ProxyMessage::Type t, void* data) noexcept;

  /**
   * Must be called from the EventBase thread;
   * drains message queue.
   */
  void drainMessageQueue();

  /**
   * @return Current value of the relaxed notification period if set.
   */
  size_t queueNotifyPeriod() const;

  McrouterInstanceBase& router() const {
    return router_;
  }

  size_t getId() const {
    return id_;
  }

  AsyncLog& asyncLog() {
    return asyncLog_;
  }

  /**
   * This method is equal to router().opts(), with the only difference,
   * that it doesn't require the caller to know about McrouterInstanceBase.
   * This allows to break include cycles.
   */
  const McrouterOptions& getRouterOptions() const;

  ProxyDestinationMap* destinationMap() const {
    return destinationMap_.get();
  }

  std::mt19937& randomGenerator() {
    return randomGenerator_;
  }

  bool beingDestroyed() const {
    return beingDestroyed_;
  }

  folly::fibers::FiberManager& fiberManager() {
    return fiberManager_;
  }

  ProxyStats& stats() {
    return stats_;
  }
  const ProxyStats& stats() const {
    return stats_;
  }

  ProxyStatsContainer* statsContainer() const {
    return statsContainer_.get();
  }

 private:
  McrouterInstanceBase& router_;

  std::unique_ptr<ProxyDestinationMap> destinationMap_;

  std::mt19937 randomGenerator_;

  ProxyStats stats_;

  std::unique_ptr<ProxyStatsContainer> statsContainer_;

  // If true, processing new requests is not safe.
  bool beingDestroyed_{false};

  folly::fibers::FiberManager fiberManager_;

  folly::EventBase* eventBase_{nullptr};

  CyclesObserver cyclesObserver_;

  AsyncLog asyncLog_;

  /** Read/write lock for config pointer */
  SFRLock configLock_;
  std::shared_ptr<McrouterProxyConfig> config_;

  // Stores the id of the next request.
  uint64_t nextReqId_ = 0;

  std::unique_ptr<MessageQueue<ProxyMessage>> messageQueue_;

  struct ProxyDelayedDestructor {
    void operator()(Proxy* proxy) {
      /* We only access self_ during construction, so this code should
         never run concurrently.

         Note: not proxy->self_.reset(), since this could destroy client
         from inside the call to reset(), destroying self_ while the method
         is still running. */
      auto stolenPtr = std::move(proxy->self_);
    }
  };

  std::shared_ptr<Proxy> self_;
  size_t id_{0};

  using Pointer = std::unique_ptr<Proxy, ProxyDelayedDestructor>;
  static Pointer createProxy(McrouterInstanceBase& router,
                             folly::EventBase& eventBase,
                             size_t id);
  Proxy(McrouterInstanceBase& router, size_t id);

  void messageReady(ProxyMessage::Type t, void* data);

  /** Process and reply stats request */
  void routeHandlesProcessRequest(
      const McStatsRequest& req,
      std::unique_ptr<ProxyRequestContextTyped<
        McrouterRouteHandleIf, McStatsRequest>> ctx);

  /** Process and reply to a version request */
  void routeHandlesProcessRequest(
      const McVersionRequest& req,
      std::unique_ptr<ProxyRequestContextTyped<
        McrouterRouteHandleIf, McVersionRequest>> ctx);

  /** Route request through route handle tree */
  template <class Request>
  typename std::enable_if<TRequestListContains<Request>::value, void>::type
  routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
          ctx);

  /** Fail all unknown operations */
  template <class Request>
  typename std::enable_if<!TRequestListContains<Request>::value, void>::type
  routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
          ctx);

  /** Process request (update stats and route the request) */
  template <class Request>
  void processRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
          ctx);

  /** Increase requests sent stats counters for given operation type */
  template <class Request>
  void bumpStats(const Request&);

  /**
   * Incoming request rate limiting.
   *
   * We need this to protect memory and CPU intensive routing code from
   * processing too many requests at a time. The limit here ensures that
   * in an event we get a spike of incoming requests, we'll queue up
   * proxy_request_t objects, which don't consume nearly as much memory as
   * fiber stacks.
   */

  /** Number of requests processing */
  size_t numRequestsProcessing_{0};
  /** Number of waiting requests */
  size_t numRequestsWaiting_{0};

  /**
   * We use this wrapper instead of putting 'hook' inside ProxyRequestContext
   * directly due to an include cycle:
   * proxy.h -> ProxyRequestContext.h -> ProxyRequestLogger.h ->
   * ProxyRequestLogger-inl.h -> proxy.h
   */
  class WaitingRequestBase {
    UniqueIntrusiveListHook hook;

   public:
    using Queue =
        UniqueIntrusiveList<WaitingRequestBase, &WaitingRequestBase::hook>;

    virtual ~WaitingRequestBase() = default;

    /**
     * Continue processing proxy request.
     *
     * We lose any information about the type when we enqueue request as
     * waiting. The inheritance allows us to resume where we left and continues
     * processing requests retaining all the type information
     * (e.g. Operation and Request).
     */
    virtual void process(Proxy* proxy) = 0;
  };

  template <class Request>
  class WaitingRequest : public WaitingRequestBase {
   public:
    WaitingRequest(
        const Request& req,
        std::unique_ptr<
            ProxyRequestContextTyped<McrouterRouteHandleIf, Request>> ctx);
    void process(Proxy* proxy) override final;
    void setTimePushedOnQueue(int64_t now) { timePushedOnQueue_ = now; }

   private:
    const Request& req_;
    std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
        ctx_;

    int64_t timePushedOnQueue_{-1};
  };

  /** Queue of requests we didn't start processing yet */
  WaitingRequestBase::Queue
      waitingRequests_[static_cast<int>(ProxyRequestPriority::kNumPriorities)];

  /** If true, we can't start processing this request right now */
  template <class Request>
  bool rateLimited(ProxyRequestPriority priority, const Request&) const;

  /** Will let through requests from the above queue if we have capacity */
  void pump();

  /**
   * Returns the next request id.
   * Request ids are unique per Proxy.
   */
  uint64_t nextRequestId();

  friend class McrouterClient;
  friend class McrouterInstance;
  friend class ProxyRequestContext;
  friend class ProxyThread;
};

struct old_config_req_t {
  explicit old_config_req_t(std::shared_ptr<McrouterProxyConfig> config)
    : config_(std::move(config)) {
  }
 private:
  std::shared_ptr<McrouterProxyConfig> config_;
};

void proxy_config_swap(
    Proxy* proxy,
    std::shared_ptr<McrouterProxyConfig> config);
}}} // facebook::memcache::mcrouter

#include "Proxy-inl.h"
