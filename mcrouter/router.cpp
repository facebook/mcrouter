/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "router.h"

#include <event.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capability.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <string>
#include <thread>
#include <unordered_map>

#include <boost/filesystem/operations.hpp>

#include <folly/Conv.h>
#include <folly/DynamicConverter.h>
#include <folly/experimental/Singleton.h>
#include <folly/Format.h>
#include <folly/io/async/EventBase.h>
#include <folly/json.h>
#include <folly/MapUtil.h>
#include <folly/Memory.h>
#include <folly/ThreadName.h>

#include "mcrouter/_router.h"
#include "mcrouter/async.h"
#include "mcrouter/config.h"
#include "mcrouter/FileObserver.h"
#include "mcrouter/flavor.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/fbi/error.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/priorities.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/RuntimeVarsData.h"

using std::string;
using std::unordered_map;

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

/* Default thread stack size if RLIMIT_STACK is unlimited */
const size_t DEFAULT_STACK_SIZE = 8192 * 1024;

class McrouterManager {
 public:
  ~McrouterManager() {
    freeAllMcrouters();
  }

  mcrouter_t* mcrouterGetCreate(const std::string& persistence_id,
                                const McrouterOptions& options) {
    std::lock_guard<std::mutex> lg(mutex_);

    auto mcrouter = folly::get_default(mcrouters_, persistence_id, nullptr);
    if (!mcrouter) {
      mcrouter = mcrouter_new(options);
      if (mcrouter) {
        mcrouters_[persistence_id] = mcrouter;
      }
    }
    return mcrouter;
  }

  mcrouter_t* mcrouterGet(const std::string& persistence_id) {
    std::lock_guard<std::mutex> lg(mutex_);

    return folly::get_default(mcrouters_, persistence_id, nullptr);
  }

  void freeAllMcrouters() {
    std::lock_guard<std::mutex> lg(mutex_);

    for (auto& mcrouter: mcrouters_) {
      mcrouter_free(mcrouter.second);
    }

    mcrouters_.clear();
  }

 private:
  std::unordered_map<std::string, mcrouter_t*> mcrouters_;
  std::mutex mutex_;
};

folly::Singleton<McrouterManager> mcrouterManager;

}  // anonymous namespace

// return 1 if precheck find interesting request and has the reply set up
// if 0 is returned, this request needs to go through normal flow
static int precheck_request(mcrouter_queue_entry_t *mcreq) {
  FBI_ASSERT(mcreq &&
             mcreq->request &&
             mcreq->request->op >= 0 &&
             mcreq->request->op < mc_nops);

  switch (mcreq->request->op) {
    // Return error (pretend to not even understand the protocol)
    case mc_op_shutdown:
      mcreq->reply = McReply(mc_res_bad_command);
      break;

    // Return 'Not supported' message
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_flushall:
    case mc_op_flushre:
      mcreq->reply = McReply(mc_res_remote_error, "Command not supported");
      break;

    // Everything else is supported
    default:
      auto err = mc_client_req_check(mcreq->request);
      if (err != mc_req_err_valid) {
        mcreq->reply = McReply(mc_res_remote_error, mc_req_err_to_string(err));
        break;
      }
      return 0;
  }
  return 1;
}

void mcrouter_enqueue_reply(proxy_request_t *preq);

void mcrouter_request_ready_cb(asox_queue_t q,
                               asox_queue_entry_t *entry,
                               void *arg) {
  if (entry->type == request_type_request) {
    proxy_t *proxy = (proxy_t*)arg;
    proxy_request_t *preq = nullptr;
    mcrouter_queue_entry_t* router_entry = (mcrouter_queue_entry_t*)entry->data;
    auto client = router_entry->router_client;

    client->numPending_++;

    FBI_ASSERT(entry->nbytes == sizeof(mcrouter_queue_entry_t*));

    if (precheck_request(router_entry)) {
      client->onReply(entry);
      return;
    }

    if (proxy->being_destroyed) {
      /* We can't process this, since 1) we destroyed the config already,
         and 2) the clients are winding down, so we wouldn't get any
         meaningful response back anyway. */
      LOG(ERROR) << "Outstanding request on a proxy that's being destroyed";
      client->onReply(entry);
      return;
    }

    // this will also grab a reference to the router_entry->request
    // so free the reference from the enqueue below
    try {
      /* steal router_entry->request, so we don't need to mc_msg_decref() */
      preq = new proxy_request_t(proxy,
                                 McMsgRef::moveRef(router_entry->request),
                                 mcrouter_enqueue_reply,
                                 router_entry->context,
                                 /* reqComplete= */ nullptr,
                                 client->clientId_);
      if (router_entry->saved_request.hasValue()) {
        preq->saved_request.emplace(std::move(*router_entry->saved_request));
      }
    } catch (...) {
      preq = nullptr;
      LOG(ERROR) << "Failed to create proxy_request";
    }
    // we weren't able to construct a preq so pass it back
    // to the client as an error
    if (!preq) {
      router_entry->reply = McReply(mc_res_local_error,
                                    "Couldn't create proxy_request_t");
      client->onReply(entry);
      return;
    }
    preq->requester = client->incref();
    proxy->dispatchRequest(preq);

    delete router_entry;
    proxy_request_decref(preq);
  } else if (entry->type == request_type_old_config) {
    auto oldConfig = (old_config_req_t*) entry->data;
    delete oldConfig;
  } else if (entry->type == request_type_disconnect) {
    auto client = (McrouterClient*) entry->data;
    client->disconnected_ = true;
    if (client->numPending_ == 0) client->cleanup();
  } else if (entry->type == request_type_router_shutdown) {
    /*
     * No-op. We just wanted to wake this event base up so that
     * it can exit event loop and check router->shutdown
     */
  } else {
    LOG(ERROR) << "CRITICAL: Unrecognized request type " << entry->type << "!";
    FBI_ASSERT(0);
  }
}

void mcrouter_set_thread_name(pthread_t tid,
                              const McrouterOptions& opts,
                              folly::StringPiece prefix) {
  auto name = folly::format("{}-{}", prefix, opts.router_name).str();
  if (!folly::setThreadName(tid, name)) {
    LOG(WARNING) << "Unable to set thread name to " << name;
  }
}

static void *stat_updater_thread_run(void *arg) {
  mcrouter_t* router = (mcrouter_t*)arg;
  if (router->opts.num_proxies <= 0) {
    return nullptr;
  }

  // the idx of the oldest bin
  int idx = 0;
  static const int BIN_NUM = (MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
                            MOVING_AVERAGE_BIN_SIZE_IN_SECOND);

  while (true) {
    {
      /* Wait for the full timeout unless shutdown is started */
      std::unique_lock<std::mutex> lock(router->statUpdaterCvMutex);
      if (router->statUpdaterCv.wait_for(
            lock,
            std::chrono::seconds(MOVING_AVERAGE_BIN_SIZE_IN_SECOND),
            [router]() { return router->shutdownStarted(); })) {
        /* Shutdown was initiated, so we stop this thread */
        break;
      }
    }

    // to avoid inconsistence among proxies, we lock all mutexes together
    for (size_t i = 0; i < router->opts.num_proxies; ++i) {
      router->getProxy(i)->stats_lock.lock();
    }

    for (size_t i = 0; i < router->opts.num_proxies; ++i) {
      auto proxy = router->getProxy(i);
      if (proxy->num_bins_used < BIN_NUM) {
        ++proxy->num_bins_used;
      }

      for(int j = 0; j < num_stats; ++j) {
        if (proxy->stats[j].group & rate_stats) {
          proxy->stats_num_within_window[j] -= proxy->stats_bin[j][idx];
          proxy->stats_bin[j][idx] = proxy->stats[j].data.uint64;
          proxy->stats_num_within_window[j] += proxy->stats_bin[j][idx];
          proxy->stats[j].data.uint64 = 0;
        }
      }
    }

    for (size_t i = 0; i < router->opts.num_proxies; ++i) {
      router->getProxy(i)->stats_lock.unlock();
    }

    idx = (idx + 1) % BIN_NUM;
  }
  return nullptr;
}

int spawn_thread(pthread_t *thread_handle, void **stack,
                 void *(thread_run)(void*), void *arg, int realtime) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  size_t stack_sz =
    rlim.rlim_cur == RLIM_INFINITY ? DEFAULT_STACK_SIZE : rlim.rlim_cur;
  FBI_VERIFY(posix_memalign(stack, 8, stack_sz) == 0);
  FBI_VERIFY(pthread_attr_setstack(&attr, *stack, stack_sz) == 0);


  if (realtime) {
    struct sched_param sched_param;
    cap_t cap_p = cap_get_proc();
    cap_flag_value_t cap_val;
    cap_get_flag(cap_p, CAP_SYS_NICE, CAP_EFFECTIVE, &cap_val);
    if (cap_val == CAP_SET) {
      sched_param.sched_priority = DEFAULT_REALTIME_PRIORITY_LEVEL;
      pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
      pthread_attr_setschedparam(&attr, &sched_param);
      pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }
    cap_free(cap_p);
  }

  int rc = pthread_create(thread_handle, &attr, thread_run, arg);
  pthread_attr_destroy(&attr);

  if (rc != 0) {
    *thread_handle = 0;
    LOG(ERROR) << "CRITICAL: Failed to create thread";
    return 0;
  }

  return 1;
}

static bool is_valid_router_name(const std::string& name) {
  if (name.empty()) {
    return false;
  }

  for (auto c : name) {
    if (!((c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') ||
          (c == '_') ||
          (c == '-'))) {
      return false;
    }
  }

  return true;
}

mcrouter_t::mcrouter_t(const McrouterOptions& input_options) :
    opts(options::substituteTemplates(input_options)),
    command_args(nullptr),
    prepare_proxy_server_stats(nullptr),
    pid(getpid()),
    next_proxy(0),
    configApi(createConfigApi(opts)),
    start_time(0),
    last_config_attempt(0),
    config_failures(0),
    stat_updater_thread_handle(0),
    stat_updater_thread_stack(nullptr),
    is_transient(false),
    live_clients(0),
    startupLock(opts.sync ? 0 : opts.num_proxies + 1),
    awriter(folly::make_unique<AsyncWriter>()),
    stats_log_writer(folly::make_unique<AsyncWriter>(
        opts.stats_async_queue_length)) {
  fb_timer_set_cycle_timer_func(
    []() -> uint64_t { return nowUs(); },
    1.0);
}

mcrouter_t::~mcrouter_t() {}

void mcrouter_t::startObservingRuntimeVarsFile() {
  boost::system::error_code ec;
  if (opts.runtime_vars_file.empty() ||
      !boost::filesystem::exists(opts.runtime_vars_file, ec)) {
    return;
  }

  auto& rtVarsDataRef = rtVarsData;
  auto onUpdate = [&rtVarsDataRef](std::string data) {
    rtVarsDataRef.set(std::make_shared<const RuntimeVarsData>(std::move(data)));
  };

  FileObserver::startObserving(
    opts.runtime_vars_file,
    taskScheduler_,
    opts.file_observer_poll_period_ms,
    opts.file_observer_sleep_before_update_ms,
    std::move(onUpdate)
  );
}

unordered_map<string, std::pair<bool, size_t>> mcrouter_t::getSuspectServers() {
  unordered_map<string, std::pair<bool, size_t>> result;
  pclient_owner.foreach_shared_synchronized(
    [&result](const std::string& key, ProxyClientShared& shared) {
      auto failureCount = shared.tko.consecutiveFailureCount();
      if (failureCount > 0) {
        result.emplace(key, std::make_pair(shared.tko.isTko(), failureCount));
      }
    });
  return result;
}

unordered_map<string, string> mcrouter_t::getStartupOpts() const {
  auto result = opts.toDict();
  result.insert(additionalStartupOpts_.begin(), additionalStartupOpts_.end());
  return result;
}

void mcrouter_t::addStartupOpts(unordered_map<string, string> additionalOpts) {
  additionalStartupOpts_.insert(additionalOpts.begin(), additionalOpts.end());
}

std::string mcrouter_t::routerName() const {
  return "libmcrouter." + opts.service_name + "." + opts.router_name;
}

proxy_t* mcrouter_t::getProxy(size_t index) const {
  if (!proxies_.empty()) {
    assert(proxyThreads_.empty());
    return index < proxies_.size() ? proxies_[index].get() : nullptr;
  } else {
    assert(proxies_.empty());
    return index < proxyThreads_.size() ?
                   &proxyThreads_[index]->proxy() : nullptr;
  }
}

mcrouter_t *mcrouter_new(const McrouterOptions& input_options,
                         bool spawnProxyThreads) {
  if (!is_valid_router_name(input_options.service_name) ||
      !is_valid_router_name(input_options.router_name)) {
    throw mcrouter_exception(
      "Invalid service_name or router_name provided; must be"
      " strings matching [a-zA-Z0-9_-]+");
  }

  if (!input_options.async_spool.empty()) {
    auto rc = ::access(input_options.async_spool.c_str(), W_OK);
    PLOG_IF(WARNING, rc) << "Error while checking spooldir (" <<
                            input_options.async_spool << ")";
  }

  mcrouter_t *router = new mcrouter_t(input_options);

  folly::json::serialization_opts jsonOpts;
  jsonOpts.sort_keys = true;
  folly::dynamic dict = folly::dynamic::object
    ("opts", folly::toDynamic(router->getStartupOpts()))
    ("version", MCROUTER_PACKAGE_STRING);
  auto jsonStr = folly::json::serialize(dict, jsonOpts);
  failure::setServiceContext(router->routerName(), jsonStr.toStdString());

  // Initialize client_list now as it is accessed in mcrouter_free
  if (!router) {
    LOG(ERROR) << "unable to create router";
    return nullptr;
  }

  for (size_t i = 0; i < router->opts.num_proxies; i++) {
    try {
      auto proxy =
        folly::make_unique<proxy_t>(router, nullptr, router->opts);
      if (!router->opts.standalone && spawnProxyThreads) {
        router->proxyThreads_.emplace_back(
          folly::make_unique<ProxyThread>(std::move(proxy)));
      } else {
        router->proxies_.emplace_back(std::move(proxy));
      }
    } catch (...) {
      LOG(ERROR) << "Failed to create proxy";
      mcrouter_free(router);
      return nullptr;
    }
  }

  if (!router_configure(router)) {
    LOG(ERROR) << "Failed to configure proxies";
    mcrouter_free(router);
    return nullptr;
  }

  router->start_time = time(nullptr);

  /*
   * If we're standalone, someone else will decide how to run the proxy
   * Specifically, we'll run them under proxy servers in main()
   */
  if (!router->opts.standalone && !router->opts.sync && spawnProxyThreads) {
    for (auto& pt : router->proxyThreads_) {
      int rc = pt->spawn();
      if (!rc) {
        LOG(ERROR) << "Failed to start proxy thread";
        mcrouter_free(router);
        return nullptr;
      }
    }
  }

  try {
    router->configApi->startObserving();
    router->subscribeToConfigUpdate();
  } catch (...) {
    LOG(ERROR) << "Failed to start config thread";
    mcrouter_free(router);
    return nullptr;
  }

  try {
    router->spawnAuxiliaryThreads();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    mcrouter_free(router);
    return nullptr;
  }

  router->is_transient = false;
  router->live_clients = 0;

  router->startupLock.notify();

  return router;
}

void mcrouter_t::spawnAuxiliaryThreads() {
  startAwriterThreads();
  startObservingRuntimeVarsFile();
  spawnStatUpdaterThread();
  spawnStatLoggerThread();
}

void mcrouter_t::spawnStatUpdaterThread() {
  int rc = spawn_thread(&stat_updater_thread_handle,
                        &stat_updater_thread_stack,
                        &stat_updater_thread_run,
                        this, wantRealtimeThreads());
  if (!rc) {
    throw std::runtime_error("failed to spawn mcrouter stat updater thread");
  }

  folly::setThreadName(stat_updater_thread_handle, "mcrtr-stats");
}

void mcrouter_t::startAwriterThreads() {
  if (!opts.asynclog_disable) {
    if (!awriter->start("mcrtr-awriter")) {
      throw std::runtime_error("failed to spawn mcrouter awriter thread");
    }
  }

  if (!stats_log_writer->start("mcrtr-statsw")) {
    throw std::runtime_error("failed to spawn mcrouter stats writer thread");
  }
}

void mcrouter_t::stopAwriterThreads() {
  awriter->stop();
  stats_log_writer->stop();
}

void mcrouter_t::spawnStatLoggerThread() {
  mcrouterLogger = createMcrouterLogger(this);
  mcrouterLogger->start();
}

void mcrouter_t::subscribeToConfigUpdate() {
  auto rtr = this;
  configUpdateHandle_ = configApi->subscribe([rtr]() {
    // we need to wait until all proxies have event base attached
    rtr->startupLock.wait();

    if (router_configure(rtr)) {
      rtr->onReconfigureSuccess.notify();
    } else {
      LOG(ERROR) << "Error while reconfiguring mcrouter after config change";
    }
  });
}

void mcrouter_t::unsubscribeFromConfigUpdate() {
  configUpdateHandle_.reset();
}

void mcrouter_t::shutdownAndJoinAuxiliaryThreads() {
  shutdownLock_.shutdownOnce(
    [this]() {
      statUpdaterCv.notify_all();
      joinAuxiliaryThreads();
    });
}

void mcrouter_t::joinAuxiliaryThreads() {
  /* pid check is a huge hack to make PHP fork() kinda sorta work.
     After fork(), the child doesn't have the thread but does have
     the full copy of the stack which we must cleanup. */
  if (getpid() == pid) {
    taskScheduler_.shutdownAllTasks();
    if (stat_updater_thread_handle) {
      pthread_join(stat_updater_thread_handle, nullptr);
    }
  } else {
    taskScheduler_.forkWorkAround();
  }

  if (mcrouterLogger) {
    mcrouterLogger->stop();
  }

  if (stat_updater_thread_stack) {
    free(stat_updater_thread_stack);
    stat_updater_thread_stack = nullptr;
  }

  stopAwriterThreads();
}

bool mcrouter_t::shutdownStarted() {
  return shutdownLock_.shutdownStarted();
}

ShutdownLock& mcrouter_t::shutdownLock() {
  return shutdownLock_;
}

bool mcrouter_t::wantRealtimeThreads() const {
  return opts.standalone && !opts.realtime_disabled;
}

void mcrouter_free(mcrouter_t *router) {
  if (!router) {
    return;
  }

  // Mark all the clients still attached to this router as zombie
  {
    std::lock_guard<std::mutex> guard(router->client_list_lock);
    for (auto& client : router->clientList_) {
      client.isZombie_ = true;
    }
  }

  router->unsubscribeFromConfigUpdate();
  router->configApi->stopObserving(router->pid);

  router->shutdownAndJoinAuxiliaryThreads();

  if (!router->opts.sync) {
    for (auto& pt : router->proxyThreads_) {
      pt->stopAndJoin();
    }
  }
  if (router->onDestroyProxy) {
    for (size_t i = 0; i < router->proxies_.size(); ++i) {
      router->onDestroyProxy(i, router->proxies_[i].release());
    }
  }

  router->proxies_.clear();
  router->proxyThreads_.clear();

  delete router;
}

/*
 * In contrast with mcrouter_new, mcrouter_init will check
 * for a persistent mcrouter, and make a new persistent one
 * if it doesn't exist. mcrouter_init is for use with libmcrouter
 */
mcrouter_t* mcrouter_init(const std::string& persistence_id,
                          const McrouterOptions& options) {
  // For the standalone case in libmcrouter, create a new router and return it.
  if (options.standalone) {
    LOG(INFO) << "Constructing standalone mcrouter";
    return mcrouter_new(options);
  }

  return mcrouterManager.get()->mcrouterGetCreate(persistence_id, options);
}

mcrouter_t* mcrouter_get(const std::string& persistence_id) {
  return mcrouterManager.get()->mcrouterGet(persistence_id);
}

mcrouter_t* mcrouter_new_transient(const McrouterOptions& options) {
  mcrouter_t* router = mcrouter_new(options);
  if (router != nullptr) {
    router->is_transient = true;
  }
  return router;
}

void mcrouter_enqueue_reply(proxy_request_t *preq) {
  asox_queue_entry_t entry;
  mcrouter_queue_entry_t *router_entry = new mcrouter_queue_entry_t();

  stat_incr_safe(preq->proxy->stats, mcrouter_queue_entry_num_outstanding_stat);
  router_entry->request = mc_msg_incref(const_cast<mc_msg_t*>(
                                          preq->orig_req.get()));

  router_entry->reply = std::move(preq->reply);

  router_entry->context = preq->context;
  router_entry->proxy = preq->proxy;

  entry.data = router_entry;
  entry.nbytes = sizeof(router_entry);
  entry.type = entry.priority = 0;
  preq->requester->onReply(&entry);
  FBI_ASSERT(preq->_refcount >= 1);
}

void mcrouter_t::onClientDestroyed() {
  if (is_transient && live_clients > 0) {
    std::thread shutdown_thread{
      [this]() {
        mcrouter_free(this);
      }};

    shutdown_thread.detach();
  }
}

const McrouterOptions& mcrouter_get_opts(mcrouter_t *router) {
  return router->opts;
}

void free_all_libmcrouters() {
  mcrouterManager.get()->freeAllMcrouters();
}

}}} // facebook::memcache::mcrouter
