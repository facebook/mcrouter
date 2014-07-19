/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <dirent.h>
#include <event.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <memory>
#include <random>
#include <string>

#include "folly/Range.h"
#include "mcrouter/lib/fbi/asox_queue.h"
#include "mcrouter/lib/fbi/cpp/AtomicSharedPtr.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/Observable.h"
#include "mcrouter/ThreadReadLock.h"
#include "mcrouter/awriter.h"
#include "mcrouter/config.h"
#include "mcrouter/options.h"
#include "mcrouter/stats.h"
#include "mcrouter/lib/fibers/FiberManager.h"

// make sure MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND can be exactly divided by
// MOVING_AVERAGE_BIN_SIZE_IN_SECOND
// the window size within which average stat rate is calculated
#define MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND (60 * 4)

// the bin size for average stat rate
#define MOVING_AVERAGE_BIN_SIZE_IN_SECOND (1)

namespace folly {
class dynamic;
}

class fb_timer_s;
typedef class fb_timer_s fb_timer_t;

namespace facebook { namespace memcache {

class AsyncMcClient;
class WeightedCh3HashFunc;

namespace mcrouter {
// forward declaration
class dynamic_stat_t;
class mcrouter_t;
class ProxyConfigIf;
class ProxyClientCommon;
class ProxyClientShared;
class ProxyClientOwner;
class ProxyDestination;
class ProxyDestinationMap;
class ProxyLogger;
class ProxyPool;
class RateLimiter;
class RuntimeVarsData;

typedef Observable<std::shared_ptr<const RuntimeVarsData>>
  ObservableRuntimeVars;


/** server selection policy for each pool */
enum proxy_hash_policy_t {
  proxy_hash_crc32 = 0,
  proxy_hash_ch2 = 1,
  proxy_hash_ch3 = 2,
  proxy_hash_latest = 3,
  proxy_hash_shard = 4,
  proxy_hash_const_shard = 5,
  proxy_hash_wch3 = 6,
  proxy_hash_unknown = 7,
};

enum reply_state_t {
  REPLY_STATE_NO_REPLY,
  REPLY_STATE_REPLIED,
  REPLY_STATE_REPLY_DELAYED,
};

/**
 * @struct proxy_request_t
 * @brief Stores a proxy request
 */
struct proxy_request_t {
  proxy_t* proxy; ///< For convenience

  McMsgRef orig_req; ///< Reference to the incoming request

  McMsgRef reply; /**< The reply that has been sent out for this request */

  /** Whether we have replied, and therefore have handed the request back */
  reply_state_t reply_state;
  int delay_reply;

  int failover_disabled; ///< true if no failover allowed

  int _refcount;

  uint64_t sender_id;

  uint64_t start_time_up;
  uint64_t start_time_down;
  uint64_t send_time;
  uint64_t created_time;

  /// Reference to the client that issued this
  struct mcrouter_client_t *requester;

  /**
   * For compatibility, we support (get, __mcrouter__.key) as an alias for
   * (get-service-info, key).  If this flag is set, we must undo this op
   * munging when returning a reply.
   */
  bool legacy_get_service_info;

  void *context;

  /**
   * Constructor
   *
   * @param p proxy that is satisfying the request
   * @param req the original request
   * @param enqReply the function we will use to enqueue the reply when done
   * @param con the context for this request
   * @param reqComplete if non-null, will be called when all replies
   * @param senderId the id to distinguish the request from different caller
   *   (even async) complete.
   */
  proxy_request_t(proxy_t* p,
                  McMsgRef req,
                  void (*enqReply)(proxy_request_t* preq),
                  void *con,
                  void (*reqComplete)(proxy_request_t* preq) = nullptr,
                  uint64_t senderId = 0);

  /**
   * Destructor
   */
  ~proxy_request_t();

  /**
   * Sets the reply for this proxy request and sends it out
   * @param newReply the message that we are sending out as the reply
   *        for the request we are currently handling
   */
  void sendReply(McMsgRef newReply);

  void continueSendReply();

  /**
   * @DEPRECATED
   *
   * Checks if the funciton pointer passed in is the same as the one this proxy
   * request is using for enqueue-ing replies.
   *
   * @param funcPtr the function pointer to compare
   *
   * @returns true if funcPtr equals the enqueue reply function pointer
   */
  bool enqueueReplyEquals(void (*funcPtr)(proxy_request_t* preq));

 private:
  /**
   * We don't want config to be destroyed during request
   */
  std::shared_ptr<const ProxyConfigIf> config_;
  /**
   * The function that will be called when the reply is ready
   */
  void (*enqueueReply_)(proxy_request_t* preq);

  /**
   * The function that will be called when all replies (including async)
   * come back.
   * Guaranteed to be called after enqueueReply_ (right after in sync mode).
   */
  void (*reqComplete_)(proxy_request_t* preq);

  /** Links this proxy_request into proxy's waiting queue */
  TAILQ_ENTRY(proxy_request_t) entry_;

  /** If true, this is currently being processed by a proxy and
      we want to notify we're done on destruction. */
  bool processing_;

  friend class proxy_t;

  friend void proxy_request_decref(proxy_request_t* preq);
};

/** If present, a proxy_t's proxy_client_monitor_t will be informed
 *  of events that may be useful to determine the health of a client,
 *  and will be consulted to determine whether requests may be sent to
 *  a client (whether the client is considered to be tko). */
typedef struct proxy_client_monitor proxy_client_monitor_t;
struct proxy_client_monitor {
  /** Called on successful lazy establishment of a client connection. */
  void (*on_up)(proxy_client_monitor_t *mon, ProxyDestination* pdstn);

  /** Called when libmc.client finishes a request. */
  void (*on_response)(proxy_client_monitor_t *mon, ProxyDestination* pdstn,
                      proxy_request_t *preq, mc_msg_t *req, mc_msg_t *reply,
                      mc_res_t result);

  /** Called when a connection or reconnection attempt fails. */
  void (*on_down)(proxy_client_monitor_t *mon, ProxyDestination* pdstn);

  /** Called to determine if req may be sent to pclient. Return false to
   *  prevent the send, possibly resulting in failover. */
  int (*may_send)(proxy_client_monitor_t *mon, ProxyDestination* pdstn,
                  mc_msg_t *req);

  /** Called when a proxy_client_t is freed. */
  void (*remove_client)(proxy_client_monitor_t *mon, ProxyDestination* pdstn);

  /** For the creator's use */
  void* context;
};

enum proxy_pool_failover_t {
  FAILOVER_TKO,
  FAILOVER_CONNECT_TIMEOUT,
  FAILOVER_DATA_TIMEOUT,
  FAILOVER_DATA_MISS,
  FAILOVER_MAX            // Must be last
};
#define FAILOVER_NONE   FAILOVER_MAX

enum proxy_pool_type_t {
  REGULAR_POOL = 0,
  REGIONAL_POOL,
  MIGRATED_POOL,
  NUM_POOL_TYPES
};

enum proxy_pool_failover_type_t {
  /**
   * Use region config failover policy.
   */
  POOL_FAILOVER_NO_OVERRIDE = 0,  // Must be 0 for memset to work!

  /**
   * Always failover for this failover type/op; ignore region config.
   */
  POOL_FAILOVER_ALWAYS,

  /**
   * Never failover for this failover type/op; ignore region config.
   */
  POOL_FAILOVER_NEVER,

  POOL_FAILOVER_NUM_TYPES
};

struct proxy_pool_failover_policy_t {
  proxy_pool_failover_type_t op[FAILOVER_MAX][mc_nops];

  proxy_pool_failover_policy_t();
};

enum shadow_policy_t {
  DEFAULT_SHADOW_POLICY = 0,
  MEMCACHE_SHADOW_POLICY,
  TAO_SHADOW_POLICY,
  NUM_SHADOW_POLICIES // Must be the last entry
};
#define NO_SHADOW_POLICY NUM_SHADOW_POLICIES

struct proxy_pool_shadowing_policy_t {
  struct Data {
    size_t start_index;
    size_t end_index;
    double start_key_fraction;
    double end_key_fraction;
    std::string index_range_rv;
    std::string key_fraction_range_rv;
    ProxyPool* shadow_pool;
    shadow_policy_t shadow_type;
    bool validate_replies;

    Data();

    explicit Data(const folly::dynamic& json);
  };

  proxy_pool_shadowing_policy_t(const folly::dynamic& json,
                                mcrouter_t* router);

  proxy_pool_shadowing_policy_t(std::shared_ptr<Data> data,
                                mcrouter_t* router);
  ~proxy_pool_shadowing_policy_t();

  std::shared_ptr<const Data> getData();

 private:
  AtomicSharedPtr<Data> data_;
  ObservableRuntimeVars::CallbackHandle handle_;
  void registerOnUpdateCallback(mcrouter_t* router);
};

class ProxyGenericPool {
 public:
  virtual proxy_pool_type_t getType() const = 0;

  std::string getName() const {
    return name_;
  }

  explicit ProxyGenericPool(std::string name)
  : name_(std::move(name)) {
  }

  virtual ~ProxyGenericPool() {
  }

 private:
  std::string name_;
};

class ProxyPool : public ProxyGenericPool {
 public:
  std::vector<std::weak_ptr<ProxyClientCommon>> clients;
  proxy_hash_policy_t hash;
  mc_protocol_t protocol;
  mc_transport_t transport;
  int delete_time;
  std::string hash_salt;
  std::unique_ptr<WeightedCh3HashFunc> wch3_func;
  StringKeyedUnorderedMap<size_t> shard_map;
  timeval_t timeout;
  int keep_routing_prefix;

  /**
   * If true, don't write failed requests to asynclog (regardless of
   * global asynclog_disable setting).
   */
  bool devnull_asynclog;

  /** List of pools to failover to for various scenarios */
  std::vector<ProxyGenericPool*> failover[FAILOVER_MAX];
  int failover_exptime;

  /**
   * If non-null, overrides proxy failover policy for this pool.
   */
  proxy_pool_failover_policy_t* pool_failover_policy;

  /**
   * If nonempty, describes how to shadow requests sent to this pool.
   */
  std::vector<std::shared_ptr<proxy_pool_shadowing_policy_t>>
      shadowing_policies;

  std::unique_ptr<RateLimiter> rate_limiter;

  explicit ProxyPool(std::string name);

  virtual ~ProxyPool();
};

class ProxyRegionalPool : public ProxyPool {
 public:
  virtual proxy_pool_type_t getType() const {
    return REGIONAL_POOL;
  }

  explicit ProxyRegionalPool(std::string name)
      : ProxyPool(std::move(name)) {
  }
};

class ProxyRegularPool : public ProxyPool {
 public:
  virtual proxy_pool_type_t getType() const {
    return REGULAR_POOL;
  }

  explicit ProxyRegularPool(std::string name)
      : ProxyPool(std::move(name)) {
  }
};

class ProxyMigratedPool : public ProxyGenericPool {
 public:
  virtual proxy_pool_type_t getType() const {
    return MIGRATED_POOL;
  }

  explicit ProxyMigratedPool(std::string name);

  ProxyPool* from_pool;
  ProxyPool* to_pool;
  uint64_t migration_start_ts;
  uint64_t migration_interval_sec;
  bool warming_up;
  uint32_t warmup_exptime;
};

struct countedfd_t {
  int fd;
  int refcount;
};

class ExponentialSmoothData {
 public:
  explicit ExponentialSmoothData(double smootingFactor);
  void insertSample(double value);
  double getCurrentValue() const;

 private:
  double smoothingFactor_{0.0};
  double currentValue_{0.0};
  bool hasRegisteredFirstSample_{false};
};

struct proxy_t {
  uint64_t magic;
  mcrouter_t *router;

  /** Note: will go away once the router pointer above is guaranteed to exist */
  const McrouterOptions opts;

  asox_queue_t request_queue;
  folly::EventBase* eventBase;
  std::shared_ptr<ProxyConfigIf> config;
  std::unique_ptr<ProxyDestinationMap> destinationMap;

  /** set by proxy_set_default_route */
  std::string default_route;
  std::string default_region;
  std::string default_cluster;

  /** lock for the final config pointer swap */
  std::shared_ptr<sfrlock_t> config_lock;

  /**
   * Used to acquire config_lock for reading on the proxy thread
   * to protect from recursive locking.
   */
  ThreadReadLock proxyThreadConfigReadLock;

  // async spool related
  countedfd_t *async_fd;
  time_t async_spool_time;

  std::mutex stats_lock;
  fb_timer_t *rtt_timer; // rtt for libmcrouter (mcrouter <=> memcached)
  fb_timer_t *request_timer_up;
  fb_timer_t *request_timer_down;
  stat_t stats[num_stats];
  ExponentialSmoothData durationUs;

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
  int num_bins_used;

  proxy_client_monitor_t *monitor;

  /*
   * Asynchronous writer.
   */
  std::unique_ptr<awriter_t> awriter;
  pthread_t awriter_thread_handle;
  void *awriter_thread_stack;
  // Proxies are numbered
  int id;
  int version;

  pthread_t stats_log_writer_thread_handle;
  std::unique_ptr<awriter_t> stats_log_writer;
  void *stats_log_writer_thread_stack;

  std::mt19937 randomGenerator;

  bool being_destroyed;  /**< If true, processing new requests is not safe. */

  FiberManager fiberManager;

  /**
   * Logs mcrouter stats to disk every opts->stats_logging_interval
   * milliseconds
   */
  std::unique_ptr<ProxyLogger> logger;

  std::unique_ptr<ProxyStatsContainer> statsContainer;

  /**
   * perform_stats_logging: if true, this proxy thread will log mcrouter
   *                        stats to disk every opts->stats_logging_interval
   *                        milliseconds
   */
  proxy_t(mcrouter_t *router,
          folly::EventBase* eventBase,
          const McrouterOptions& opts,
          bool perform_stats_logging);

  ~proxy_t();

  /** Queue up and route the new incoming request */
  void dispatchRequest(proxy_request_t* preq);

  /**
   * Iterates over possible clients for given key on get request. This
   * includes all failovers, shadows, etc. The first client (if there is some)
   * is for "fast" route, i.e. the one we synchronously route to.
   */
  void foreachPossibleClient(
    const std::string& key,
    std::function<void(const ProxyClientCommon&)> callback) const;

  /**
   * If no event base was provided on construction, this must be called
   * before spawning the proxy.
   */
  void attachEventBase(folly::EventBase* eventBase);

 private:
  static FiberManager::Options getFiberManagerOptions(
    const McrouterOptions& opts);

  void routeHandlesProcessRequest(proxy_request_t* preq);
  void processRequest(proxy_request_t* preq);

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
  size_t numRequestsProcessing_;

  /** Queue of requests we didn't start processing yet */
  TAILQ_HEAD(RequestTailqHead, proxy_request_t);
  RequestTailqHead waitingRequests_;

  bool performStatsLogging_;

  /** If true, we can't start processing this request right now */
  bool rateLimited(const proxy_request_t* preq) const;

  /** Will let through requests from the above queue if we have capacity */
  void pump();

  /** Called once after a valid eventBase has been provided */
  void onEventBaseAttached();

  friend class proxy_request_t;
};

struct old_config_req_t {
  explicit old_config_req_t(std::shared_ptr<ProxyConfigIf> config)
    : config_(std::move(config)) {
  }
 private:
  std::shared_ptr<ProxyConfigIf> config_;
};

struct writelog_entry_t {
  proxy_request_t *preq;
  countedfd_t *fd;
  const void *buf;
  size_t size;
  int write_result;
  awriter_entry_t awentry;
  asox_queue_entry_t qentry;
};

int router_configure(mcrouter_t *router);

int router_configure(mcrouter_t *router, folly::StringPiece input);

/**
 * Spawns the required async writer threads.
 * These include the asynclog thread (if needed) and threads
 * for other less critical writes, like stats collection.
 *
 * realtime: if true, schedule the threads with realtime priority
 *
 * Returns -1 on error, 0 on success.
 */
int proxy_start_awriter_threads(proxy_t* proxy, bool realtime);

/**
 * Must be called before destroying the proxy if awriter threads were started.
 */
void proxy_stop_awriter_threads(proxy_t* proxy);

// set the monitor to nullptr to remove an existing one
void proxy_set_monitor(proxy_t*, proxy_client_monitor_t* monitor);

void proxy_request_decref(proxy_request_t*);
proxy_request_t* proxy_request_incref(proxy_request_t*);

void proxy_on_continue_reply_error(proxy_t* proxy, writelog_entry_t* e);

McMsgRef create_reply(mc_op_t op, mc_res_t result, const char *str);

folly::StringPiece getRegionFromRoutingPrefix(folly::StringPiece prefix);

void proxy_flush_rtt_stats(proxy_t *proxy);

}}} // facebook::memcache::mcrouter
