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

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <folly/Range.h>

#include "mcrouter/awriter.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/fbi/cpp/ShutdownLock.h"
#include "mcrouter/lib/fbi/cpp/StartupLock.h"
#include "mcrouter/mcrouter_client.h"
#include "mcrouter/Observable.h"
#include "mcrouter/options.h"
#include "mcrouter/pclient.h"
#include "mcrouter/PeriodicTaskScheduler.h"
#include "mcrouter/TkoCounters.h"

class asox_queue_s;
using asox_queue_t = asox_queue_s*;
class asox_queue_entry_s;
using asox_queue_entry_t = asox_queue_entry_s;

namespace facebook { namespace memcache { namespace mcrouter {

class RuntimeVarsData;
typedef Observable<std::shared_ptr<const RuntimeVarsData>>
  ObservableRuntimeVars;

class ProxyThread;

struct mcrouter_t {
  const McrouterOptions opts;

  char* command_args;
  void (*prepare_proxy_server_stats)(proxy_t*);
  pid_t pid;

  unsigned int next_proxy;
  std::mutex next_proxy_mutex;

  // Config stuff
  std::unique_ptr<ConfigApi> configApi;
  CallbackPool<> onReconfigureSuccess;

  // These next three fields are used for stats
  uint64_t start_time;
  time_t last_config_attempt;
  int config_failures;

  // Lock to get before regenerating config structure
  std::mutex config_reconfig_lock;

  // Stat updater thread updates rate stat windows for each proxy
  pthread_t stat_updater_thread_handle;
  void* stat_updater_thread_stack;

  std::mutex statUpdaterCvMutex;
  std::condition_variable statUpdaterCv;

  std::mutex client_list_lock;
  mcrouter_client_list_t client_list;

  // If true, allow only one mcrouter client and shutdown mcrouter
  // after it disconnects
  bool is_transient;

  // For a transient mcrouter, number of alive clients
  // (different from the size of client_list,
  // as a disconnected client stays alive until all the requests come back).
  //
  // Needs to be thread safe, as we create clients on the client thread
  // and destroy them on a proxy thread.
  //
  // Currently limited to one.
  std::atomic<int> live_clients;

  // Total number of boxes marked as TKO.
  TkoCounters tkoCounters;

  ProxyClientOwner pclient_owner;

  // Stores data for runtime variables.
  ObservableRuntimeVars rtVarsData;

  StartupLock startupLock;

  /**
   * Logs mcrouter stats to disk every opts->stats_logging_interval
   * milliseconds
   */
  std::unique_ptr<McrouterLogger> mcrouterLogger;

  /*
   * Asynchronous writer.
   */
  std::unique_ptr<AsyncWriter> awriter;
  std::unique_ptr<AsyncWriter> stats_log_writer;

  std::function<void(size_t, proxy_t*)> onDestroyProxy;

  explicit mcrouter_t(const McrouterOptions& input_options);

  mcrouter_t(const mcrouter_t&) = delete;
  mcrouter_t& operator=(const mcrouter_t&) = delete;

  ~mcrouter_t();

  std::unordered_map<std::string, std::string> getStartupOpts() const;
  void addStartupOpts(
    std::unordered_map<std::string, std::string> additionalOpts);

  std::string routerName() const;

  bool shutdownStarted();

  void startAwriterThreads();
  void stopAwriterThreads();

  void spawnAuxiliaryThreads();
  void shutdownAndJoinAuxiliaryThreads();
  void joinAuxiliaryThreads();

  ShutdownLock& shutdownLock();

  /**
   * @return True if we want to run with realtime threads,
   *   that is if we're running as both standalone and with realtime requested.
   */
  bool wantRealtimeThreads() const;

  void subscribeToConfigUpdate();
  void unsubscribeFromConfigUpdate();

  /**
   * @return  nullptr if index is >= opts.num_proxies,
   *          pointer to the proxy otherwise.
   */
  proxy_t* getProxy(size_t index) const;

  /**
   * @return  servers that recently received error replies.
   *          Format: {
   *            server ip => ( is server marked as TKO?, number of failures )
   *          }
   *          Only servers with positive number of failures will be returned.
   */
  std::unordered_map<std::string, std::pair<bool, size_t>> getSuspectServers();
 private:
  ShutdownLock shutdownLock_;

  // Used to shedule periodic tasks for mcrouter.
  PeriodicTaskScheduler taskScheduler_;

  ConfigApi::CallbackHandle configUpdateHandle_;

  std::unordered_map<std::string, std::string> additionalStartupOpts_;

  /**
   * Exactly one of these vectors will contain opts.num_proxies elements,
   * others will be empty.
   *
   * Standalone/sync mode: we don't startup proxy threads, so mcrouter_t
   * owns the proxies directly.
   *
   * Embedded mode: mcrouter_t owns ProxyThreads, which managed the lifetime
   * of proxies on their own threads.
   */
  std::vector<std::unique_ptr<proxy_t>> proxies_;
  std::vector<std::unique_ptr<ProxyThread>> proxyThreads_;

  void spawnStatUpdaterThread();
  void spawnStatLoggerThread();
  void startObservingRuntimeVarsFile();

  friend mcrouter_t* mcrouter_new(const McrouterOptions&, bool);
  friend void mcrouter_free(mcrouter_t*);
};

enum request_entry_type_t {
  request_type_request,
  request_type_disconnect,
  request_type_old_config,
  request_type_router_shutdown,
};

// Bridge from mcrouter client to proxies. Putting this here is the lesser
// of two evils, as this function refers to several functions in router.c
void mcrouter_request_ready_cb(asox_queue_t q,
                            asox_queue_entry_t *entry,
                            void *arg);

/*
 * Utility function for launching threads and setting thread names.
 */
int spawn_thread(pthread_t *thread_handle, void **stack,
                 void *(thread_run)(void*), void *arg, int realtime);

void mcrouter_set_thread_name(pthread_t tid,
                              const McrouterOptions& opts,
                              folly::StringPiece prefix);

}}} // facebook::memcache::mcrouter
