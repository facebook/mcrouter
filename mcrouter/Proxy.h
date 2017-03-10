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

#include <dirent.h>
#include <event.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <folly/Range.h>
#include <folly/detail/CacheLocality.h>

#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/Observable.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyRequestPriority.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/carbon/Keys.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"
#include "mcrouter/options.h"

namespace folly {
struct dynamic;
class File;
} // folly

namespace facebook {
namespace memcache {

template <class T>
class MessageQueue;

namespace mcrouter {
// forward declaration
template <class RouterInfo>
class CarbonRouterClient;
template <class RouterInfo>
class CarbonRouterInstance;
class CarbonRouterInstanceBase;
template <class RouterInfo>
class ProxyConfig;
class ProxyDestination;
class ProxyRequestContext;
template <class RouterInfo, class Request>
class ProxyRequestContextTyped;
class RuntimeVarsData;
class ShardSplitter;

using ObservableRuntimeVars =
    Observable<std::shared_ptr<const RuntimeVarsData>>;

struct ShadowSettings {
  /**
   * @return  nullptr if config is invalid, new ShadowSettings struct otherwise
   */
  static std::shared_ptr<ShadowSettings> create(
      const folly::dynamic& json,
      CarbonRouterInstanceBase& router);

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
    return {fraction >> 32, fraction & ((1UL << 32) - 1)};
  }

  /**
   * @throws std::logic_error if !(0 <= start <= end <= 1)
   */
  void setKeyRange(double start, double end);

  /**
   * Specify a list of keys to be shadowed. Cannot be mixed with index range/key
   * fraction range-based shadowing.
   */
  void setKeysToShadow(const std::vector<std::string>& keys) {
    keysToShadow_.clear();
    for (const auto& key : keys) {
      const auto hash = carbon::Keys<std::string>(key).routingKeyHash();
      keysToShadow_.emplace_back(hash, key);
    }
    std::sort(keysToShadow_.begin(), keysToShadow_.end());
  }

  const std::vector<std::tuple<uint32_t, std::string>>& keysToShadow() const {
    return keysToShadow_;
  }

 private:
  ObservableRuntimeVars::CallbackHandle handle_;
  void registerOnUpdateCallback(CarbonRouterInstanceBase& router);

  std::string keyFractionRangeRv_;
  size_t startIndex_{0};
  size_t endIndex_{0};

  // Ideally, this would just be an unordered set<Key<string>>, but we need to
  // allow for comparing to Key<IOBuf>. We can work with a vector<Key<string>>
  // sorted by routingKeyHash.
  std::vector<std::tuple<uint32_t, std::string>> keysToShadow_;

  std::atomic<uint64_t> keyRange_{0};

  bool validateReplies_{false};

  ShadowSettings() = default;
};

struct ProxyMessage {
  enum class Type { REQUEST, OLD_CONFIG, SHUTDOWN };

  Type type{Type::REQUEST};
  void* data{nullptr};

  constexpr ProxyMessage() = default;

  ProxyMessage(Type t, void* d) noexcept : type(t), data(d) {}
};

template <class RouterInfo>
class Proxy : public ProxyBase {
 public:
  ~Proxy();

  /**
   * Access to config - can only be called on the proxy thread
   * and the resulting shared_ptr can only be detroyed on the proxy thread.
   */
  std::shared_ptr<ProxyConfig<RouterInfo>> getConfigUnsafe() const;

  /**
   * Can be called from any thread.
   *
   * Returns a lock and a reference to the config.
   * The caller may only access the config through the reference
   * while the lock is held.
   */
  std::pair<std::unique_lock<SFRReadLock>, ProxyConfig<RouterInfo>&>
  getConfigLocked() const;

  /**
   * Thread-safe config swap; returns the previous contents of
   * the config pointer
   */
  std::shared_ptr<ProxyConfig<RouterInfo>> swapConfig(
      std::shared_ptr<ProxyConfig<RouterInfo>> newConfig);

  /** Queue up and route the new incoming request */
  template <class Request>
  void dispatchRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<RouterInfo, Request>> ctx);

  /**
   * Put a new proxy message into the queue.
   */
  void sendMessage(ProxyMessage::Type t, void* data) noexcept;

  /**
   * @return Current value of the relaxed notification period if set.
   */
  size_t queueNotifyPeriod() const override;

  bool beingDestroyed() const {
    return beingDestroyed_;
  }

  typename RouterInfo::RouterStats& requestStats() {
    return requestStats_;
  }

  folly::dynamic dumpRequestStats(bool filterZeroes) const override final {
    return requestStats_.dump(filterZeroes);
  }

 private:
  // If true, processing new requests is not safe.
  bool beingDestroyed_{false};

  /** Read/write lock for config pointer */
  SFRLock configLock_;
  std::shared_ptr<ProxyConfig<RouterInfo>> config_;

  typename RouterInfo::RouterStats requestStats_;

  std::unique_ptr<MessageQueue<ProxyMessage>> messageQueue_;

  static Proxy<RouterInfo>* createProxy(
      CarbonRouterInstanceBase& router,
      folly::VirtualEventBase& evb,
      size_t id);
  Proxy(
      CarbonRouterInstanceBase& router,
      size_t id,
      folly::VirtualEventBase& evb);

  void messageReady(ProxyMessage::Type t, void* data);

  /** Process and reply stats request */
  void routeHandlesProcessRequest(
      const McStatsRequest& req,
      std::unique_ptr<ProxyRequestContextTyped<RouterInfo, McStatsRequest>>
          ctx);

  /** Process and reply to a version request */
  void routeHandlesProcessRequest(
      const McVersionRequest& req,
      std::unique_ptr<ProxyRequestContextTyped<RouterInfo, McVersionRequest>>
          ctx);

  /** Route request through route handle tree */
  template <class Request>
  typename std::enable_if<
      ListContains<typename RouterInfo::RoutableRequests, Request>::value,
      void>::type
  routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<RouterInfo, Request>> ctx);

  /** Fail all unknown operations */
  template <class Request>
  typename std::enable_if<
      !ListContains<typename RouterInfo::RoutableRequests, Request>::value,
      void>::type
  routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<RouterInfo, Request>> ctx);

  /** Process request (update stats and route the request) */
  template <class Request>
  void processRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<RouterInfo, Request>> ctx);

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
        std::unique_ptr<ProxyRequestContextTyped<RouterInfo, Request>> ctx);
    void process(Proxy* proxy) override final;
    void setTimePushedOnQueue(int64_t now) {
      timePushedOnQueue_ = now;
    }

   private:
    const Request& req_;
    std::unique_ptr<ProxyRequestContextTyped<RouterInfo, Request>> ctx_;

    int64_t timePushedOnQueue_{-1};
  };

  /** Queue of requests we didn't start processing yet */
  typename WaitingRequestBase::Queue
      waitingRequests_[static_cast<int>(ProxyRequestPriority::kNumPriorities)];

  /** If true, we can't start processing this request right now */
  template <class Request>
  typename std::enable_if<TNotRateLimited<Request>::value, bool>::type
  rateLimited(ProxyRequestPriority priority, const Request&) const;

  template <class Request>
  typename std::enable_if<!TNotRateLimited<Request>::value, bool>::type
  rateLimited(ProxyRequestPriority priority, const Request&) const;

  /** Will let through requests from the above queue if we have capacity */
  void pump() override final;

  friend class CarbonRouterInstance<RouterInfo>;
  friend class CarbonRouterClient<RouterInfo>;
  friend class ProxyRequestContext;
};

template <class RouterInfo>
struct old_config_req_t {
  explicit old_config_req_t(std::shared_ptr<ProxyConfig<RouterInfo>> config)
      : config_(std::move(config)) {}

 private:
  std::shared_ptr<ProxyConfig<RouterInfo>> config_;
};

template <class RouterInfo>
void proxy_config_swap(
    Proxy<RouterInfo>* proxy,
    std::shared_ptr<ProxyConfig<RouterInfo>> config);
}
}
} // facebook::memcache::mcrouter

#include "Proxy-inl.h"
