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

#include <folly/Range.h>
#include <folly/detail/CacheLocality.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/config.h"
#include "mcrouter/CyclesObserver.h"
#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/lib/fbi/asox_queue.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"
#include "mcrouter/Observable.h"
#include "mcrouter/options.h"
#include "mcrouter/ProxyRequestPriority.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/stats.h"

// make sure MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND can be exactly divided by
// MOVING_AVERAGE_BIN_SIZE_IN_SECOND
// the window size within which average stat rate is calculated
#define MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND (60 * 4)

// the bin size for average stat rate
#define MOVING_AVERAGE_BIN_SIZE_IN_SECOND (1)

namespace folly {
  class dynamic;
  class File;
}

class fb_timer_s;
typedef class fb_timer_s fb_timer_t;

namespace facebook { namespace memcache {

template <class T>
class MessageQueue;

namespace mcrouter {
// forward declaration
class McrouterClient;
class McrouterInstance;
class ProxyConfig;
class ProxyClientCommon;
class ProxyDestination;
class ProxyDestinationMap;
class ProxyRequestContext;
template <class Operation, class Request>
class ProxyRequestContextTyped;
class RuntimeVarsData;
class ShardSplitter;

typedef Observable<std::shared_ptr<const RuntimeVarsData>>
  ObservableRuntimeVars;

struct ShadowSettings {
  /**
   * @return  nullptr if config is invalid, new ShadowSettings struct otherwise
   */
  static std::shared_ptr<ShadowSettings>
  create(const folly::dynamic& json, McrouterInstance& router);

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

  void setValidateReplies(bool validateReplies);

 private:
  ObservableRuntimeVars::CallbackHandle handle_;
  void registerOnUpdateCallback(McrouterInstance& router);

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

struct proxy_t {
  uint64_t magic;
 private:
  McrouterInstance& router_;
 public:

  std::atomic<int64_t> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING loopStart_{0};

  std::unique_ptr<ProxyDestinationMap> destinationMap;

  // async spool related
  std::shared_ptr<folly::File> async_fd{nullptr};
  time_t async_spool_time{0};

  std::mutex stats_lock;
  stat_t stats[num_stats];

  ExponentialSmoothData<64> durationUs;

  // we are wasting some memory here to get faster mapping from stat name to
  // stats_bin[] and stats_num_within_window[] entry. i.e., the stats_bin[]
  // and stats_num_within_window[] entry for non-rate stat are not in use.

  // we maintain some information for calculating average rate in the past
  // MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds for every rate stat.

  /*
   * stats_bin[stat_name] is a circular array associated with stat "stat_name",
   * where each element (stats_bin[stat_name][idx]) is the count of "stat_name"
   * in the "idx"th time bin. The updater thread updates these circular arrays
   * once every MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND second by setting the
   * oldest time bin to stats[stat_name], and then reset stats[stat_name] to 0.
   */
  uint64_t stats_bin[num_stats][MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
                                MOVING_AVERAGE_BIN_SIZE_IN_SECOND];
  /*
   * stats_num_within_window[stat_name] contains the count of stat "stat_name"
   * in the past MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds. this array is
   * also updated by the updater thread once every
   * MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds
   */
  uint64_t stats_num_within_window[num_stats];

  /*
   * the number of bins currently used, which is initially set to 0, and is
   * increased by 1 every MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds.
   * num_bins_used is at most MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
   * MOVING_AVERAGE_BIN_SIZE_IN_SECOND
   */
  int num_bins_used{0};

  std::mt19937 randomGenerator;

  /**
   * If true, processing new requests is not safe.
   */
  bool being_destroyed{false};

  folly::fibers::FiberManager fiberManager;
  CyclesObserver cyclesObserver;

  std::unique_ptr<ProxyStatsContainer> statsContainer;

  folly::EventBase& eventBase() const {
    assert(eventBase_ != nullptr);
    return *eventBase_;
  }

  ~proxy_t();

  /**
   * Thread-safe access to config
   */
  std::shared_ptr<ProxyConfig> getConfig() const;

  /**
   * Returns a lock and a reference to the config.
   * The caller may only access the config through the reference
   * while the lock is held.
   */
  std::pair<std::unique_lock<SFRReadLock>, ProxyConfig&>
  getConfigLocked() const;

  /**
   * Thread-safe config swap; returns the previous contents of
   * the config pointer
   */
  std::shared_ptr<ProxyConfig> swapConfig(
    std::shared_ptr<ProxyConfig> newConfig);

  /** Queue up and route the new incoming request */
  template <class Operation, class Request>
  void dispatchRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<Operation, Request>> ctx);

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

  McrouterInstance& router() const {
    return router_;
  }

  /**
   * This method is equal to router().opts(), with the only difference,
   * that it doesn't require the caller to know about McrouterInstance.
   * This allows to break include cycles.
   */
  const McrouterOptions& getRouterOptions() const;

 private:
  folly::EventBase* eventBase_{nullptr};

  /** Read/write lock for config pointer */
  SFRLock configLock_;
  std::shared_ptr<ProxyConfig> config_;

  // Stores the id of the next request.
  uint64_t nextReqId_ = 0;

  std::unique_ptr<MessageQueue<ProxyMessage>> messageQueue_;

  struct ProxyDelayedDestructor {
    void operator() (proxy_t* proxy) {
      /* We only access self_ during construction, so this code should
         never run concurrently.

         Note: not proxy->self_.reset(), since this could destroy client
         from inside the call to reset(), destroying self_ while the method
         is still running. */
      auto stolenPtr = std::move(proxy->self_);
    }
  };

  std::shared_ptr<proxy_t> self_;

  using Pointer = std::unique_ptr<proxy_t, ProxyDelayedDestructor>;
  static Pointer createProxy(McrouterInstance& router,
                             folly::EventBase& eventBase);
  explicit proxy_t(McrouterInstance& router);

  void messageReady(ProxyMessage::Type t, void* data);

  /** Process and reply stats request */
  template <class Request>
  void routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<
          ProxyRequestContextTyped<McOperation<mc_op_stats>, Request>> ctx);

  /** Route request through route handle tree */
  template <class Operation, class Request>
  typename std::enable_if<McOpListContains<Operation>::value, void>::type
  routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<Operation, Request>> ctx);

  /** Fail all unknown operations */
  template <class Operation, class Request>
  typename std::enable_if<!McOpListContains<Operation>::value, void>::type
  routeHandlesProcessRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<Operation, Request>> ctx);

  /** Process request (update stats and route the request) */
  template <class Operation, class Request>
  void processRequest(
      const Request& req,
      std::unique_ptr<ProxyRequestContextTyped<Operation, Request>> ctx);

  /** Increase requests sent stats counters for given operation type */
  template <class Operation>
  void bumpStats(Operation);

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
    virtual void process(proxy_t* proxy) = 0;
  };

  template <class Operation, class Request>
  class WaitingRequest : public WaitingRequestBase {
   public:
    WaitingRequest(
        const Request& req,
        std::unique_ptr<ProxyRequestContextTyped<Operation, Request>> ctx);
    void process(proxy_t* proxy) override;

   private:
    const Request& req_;
    std::unique_ptr<ProxyRequestContextTyped<Operation, Request>> ctx_;
  };

  /** Queue of requests we didn't start processing yet */
  WaitingRequestBase::Queue
      waitingRequests_[static_cast<int>(ProxyRequestPriority::kNumPriorities)];

  /** If true, we can't start processing this request right now */
  template <class Operation>
  bool rateLimited(ProxyRequestPriority priority, Operation) const;

  /** Will let through requests from the above queue if we have capacity */
  void pump();

  /**
   * Returns the next request id.
   * Request ids are unique per proxy_t.
   */
  uint64_t nextRequestId();

  friend class McrouterClient;
  friend class McrouterInstance;
  friend class ProxyRequestContext;
  friend class ProxyThread;
};

struct old_config_req_t {
  explicit old_config_req_t(std::shared_ptr<ProxyConfig> config)
    : config_(std::move(config)) {
  }
 private:
  std::shared_ptr<ProxyConfig> config_;
};

void proxy_config_swap(proxy_t* proxy,
                       std::shared_ptr<ProxyConfig> config);

}}} // facebook::memcache::mcrouter

#include "proxy-inl.h"
