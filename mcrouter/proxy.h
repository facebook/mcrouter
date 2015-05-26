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
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <atomic>
#include <memory>
#include <random>
#include <string>

#include <folly/Range.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/CyclesObserver.h"
#include "mcrouter/config.h"
#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/lib/fbi/asox_queue.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"
#include "mcrouter/Observable.h"
#include "mcrouter/options.h"
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

class McReply;

namespace mcrouter {
// forward declaration
class McrouterClient;
class McrouterInstance;
class ProxyConfig;
class ProxyConfigIf;
class ProxyClientCommon;
class ProxyDestination;
class ProxyDestinationMap;
class ProxyRequestContext;
class RuntimeVarsData;
class ShardSplitter;

typedef Observable<std::shared_ptr<const RuntimeVarsData>>
  ObservableRuntimeVars;

struct ShadowSettings {
  /**
   * @return  nullptr if config is invalid, new ShadowSettings struct otherwise
   */
  static std::shared_ptr<ShadowSettings>
  create(const folly::dynamic& json, McrouterInstance* router);

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
  void registerOnUpdateCallback(McrouterInstance* router);

  std::string keyFractionRangeRv_;
  size_t startIndex_{0};
  size_t endIndex_{0};

  std::atomic<uint64_t> keyRange_{0};

  ShadowSettings() = default;
};

struct proxy_t {
  uint64_t magic;
  McrouterInstance* router{nullptr};

  /** TODO: remove and use router->opts() instead */
  const McrouterOptions& opts;

  asox_queue_t request_queue{0};
  folly::EventBase* eventBase{nullptr};

  std::unique_ptr<ProxyDestinationMap> destinationMap;

  // async spool related
  std::shared_ptr<folly::File> async_fd{nullptr};
  time_t async_spool_time{0};

  std::mutex stats_lock;
  stat_t stats[num_stats];

  static constexpr double kExponentialFactor{1.0 / 64.0};
  ExponentialSmoothData durationUs{kExponentialFactor};

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

  ~proxy_t();

  /**
   * Thread-safe access to config
   */
  std::shared_ptr<ProxyConfigIf> getConfig() const;

  /**
   * Returns a lock and a reference to the config.
   * The caller may only access the config through the reference
   * while the lock is held.
   */
  std::pair<std::unique_lock<SFRReadLock>, ProxyConfigIf&>
  getConfigLocked() const;

  /**
   * Thread-safe config swap; returns the previous contents of
   * the config pointer
   */
  std::shared_ptr<ProxyConfigIf> swapConfig(
    std::shared_ptr<ProxyConfigIf> newConfig);

  /** Queue up and route the new incoming request */
  void dispatchRequest(std::unique_ptr<ProxyRequestContext> preq);

  /**
   * If no event base was provided on construction, this must be called
   * before spawning the proxy.
   */
  void attachEventBase(folly::EventBase* eventBase);

 private:
  proxy_t(McrouterInstance& router, folly::EventBase* eventBase);

  /** Read/write lock for config pointer */
  SFRLock configLock_;
  std::shared_ptr<ProxyConfigIf> config_;

  pthread_t awriterThreadHandle_{0};
  void* awriterThreadStack_{nullptr};

  pthread_t statsLogWriterThreadHandle_{0};
  void* statsLogWriterThreadStack_{nullptr};

  void routeHandlesProcessRequest(std::unique_ptr<ProxyRequestContext> preq);
  void processRequest(std::unique_ptr<ProxyRequestContext> preq);

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

  /**
   * We use this wrapper instead of putting 'hook' inside ProxyRequestContext
   * directly due to an include cycle:
   * proxy.h -> ProxyRequestContext.h -> ProxyRequestLogger.h ->
   * ProxyRequestLogger-inl.h -> proxy.h
   */
  struct WaitingRequest {
    UniqueIntrusiveListHook hook;
    using Queue = UniqueIntrusiveList<WaitingRequest,
                                      &WaitingRequest::hook>;
    std::unique_ptr<ProxyRequestContext> request;
    explicit WaitingRequest(std::unique_ptr<ProxyRequestContext> r);
  };

  /** Queue of requests we didn't start processing yet */
  WaitingRequest::Queue waitingRequests_;

  /** If true, we can't start processing this request right now */
  bool rateLimited(const ProxyRequestContext& preq) const;

  /** Will let through requests from the above queue if we have capacity */
  void pump();

  /** Called once after a valid eventBase has been provided */
  void onEventBaseAttached();

  friend class McrouterInstance;
  friend class ProxyRequestContext;
};

struct old_config_req_t {
  explicit old_config_req_t(std::shared_ptr<ProxyConfigIf> config)
    : config_(std::move(config)) {
  }
 private:
  std::shared_ptr<ProxyConfigIf> config_;
};

enum request_entry_type_t {
  request_type_request,
  request_type_disconnect,
  request_type_old_config,
  request_type_router_shutdown,
};

void proxy_config_swap(proxy_t* proxy,
                       std::shared_ptr<ProxyConfig> config);

}}} // facebook::memcache::mcrouter
