/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McrouterInstance.h"

#include <boost/filesystem/operations.hpp>

#include <folly/DynamicConverter.h>
#include <folly/experimental/Singleton.h>
#include <folly/json.h>
#include <folly/MapUtil.h>

#include "mcrouter/awriter.h"
#include "mcrouter/FileObserver.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/ThreadUtil.h"

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterManager {
 public:
  McrouterManager() {
    scheduleSingletonCleanup();
  }

  ~McrouterManager() {
    freeAllMcrouters();
  }

  McrouterInstance* mcrouterGetCreate(folly::StringPiece persistence_id,
                                      const McrouterOptions& options) {
    std::lock_guard<std::mutex> lg(mutex_);

    auto mcrouter = folly::get_default(mcrouters_, persistence_id.str(),
                                       nullptr);
    if (!mcrouter) {
      mcrouter = McrouterInstance::create(options);
      if (mcrouter) {
        mcrouters_[persistence_id.str()] = mcrouter;
      }
    }
    return mcrouter;
  }

  McrouterInstance* mcrouterGet(folly::StringPiece persistence_id) {
    std::lock_guard<std::mutex> lg(mutex_);

    return folly::get_default(mcrouters_, persistence_id.str(), nullptr);
  }

  void freeAllMcrouters() {
    std::lock_guard<std::mutex> lg(mutex_);

    for (auto& mcrouter: mcrouters_) {
      mcrouter.second->tearDown();
    }

    mcrouters_.clear();
  }

 private:
  std::unordered_map<std::string, McrouterInstance*> mcrouters_;
  std::mutex mutex_;
};

namespace {

folly::Singleton<McrouterManager> gMcrouterManager;

bool isValidRouterName(folly::StringPiece name) {
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

}  // anonymous namespace

McrouterInstance* McrouterInstance::init(folly::StringPiece persistence_id,
                                         const McrouterOptions& options) {
  if (auto manager = gMcrouterManager.get_weak_fast().lock()) {
    return manager->mcrouterGetCreate(persistence_id, options);
  }

  return nullptr;
}

McrouterInstance* McrouterInstance::get(folly::StringPiece persistence_id) {
  if (auto manager = gMcrouterManager.get_weak_fast().lock()) {
    return manager->mcrouterGet(persistence_id);
  }

  return nullptr;
}

McrouterInstance* McrouterInstance::create(const McrouterOptions& input_options,
                                           bool spawnProxyThreads) {
  if (!isValidRouterName(input_options.service_name) ||
      !isValidRouterName(input_options.router_name)) {
    throw std::runtime_error(
      "Invalid service_name or router_name provided; must be"
      " strings matching [a-zA-Z0-9_-]+");
  }

  if (!input_options.async_spool.empty()) {
    auto rc = ::access(input_options.async_spool.c_str(), W_OK);
    PLOG_IF(WARNING, rc) << "Error while checking spooldir (" <<
                            input_options.async_spool << ")";
  }

  auto router = new McrouterInstance(input_options);

  folly::json::serialization_opts jsonOpts;
  jsonOpts.sort_keys = true;
  folly::dynamic dict = folly::dynamic::object
    ("opts", folly::toDynamic(router->getStartupOpts()))
    ("version", MCROUTER_PACKAGE_STRING);
  auto jsonStr = folly::json::serialize(dict, jsonOpts);
  failure::setServiceContext(router->routerName(), jsonStr.toStdString());

  if (!router->spinUp(spawnProxyThreads)) {
    router->tearDown();
    return nullptr;
  }
  return router;
}

McrouterClient::Pointer McrouterInstance::createClient(
  mcrouter_client_callbacks_t callbacks,
  void* arg,
  size_t max_outstanding) {

  if (isTransient_ && liveClients_.fetch_add(1) > 0) {
    liveClients_--;
    throw std::runtime_error(
      "Can't create multiple clients with a transient mcrouter");
  }

  return McrouterClient::Pointer(
    new McrouterClient(this,
                       callbacks,
                       arg,
                       max_outstanding));
}

bool McrouterInstance::spinUp(bool spawnProxyThreads) {
  for (size_t i = 0; i < opts_.num_proxies; i++) {
    try {
      auto proxy =
        folly::make_unique<proxy_t>(this, nullptr, opts_);
      if (!opts_.standalone && spawnProxyThreads) {
        proxyThreads_.emplace_back(
          folly::make_unique<ProxyThread>(std::move(proxy)));
      } else {
        proxies_.emplace_back(std::move(proxy));
      }
    } catch (...) {
      LOG(ERROR) << "Failed to create proxy";
      return false;
    }
  }

  if (!reconfigure()) {
    LOG(ERROR) << "Failed to configure proxies";
    return false;
  }

  startTime_ = time(nullptr);

  /*
   * If we're standalone, someone else will decide how to run the proxy
   * Specifically, we'll run them under proxy servers in main()
   */
  if (!opts_.standalone && spawnProxyThreads) {
    for (auto& pt : proxyThreads_) {
      auto rc = pt->spawn();
      if (!rc) {
        LOG(ERROR) << "Failed to start proxy thread";
        return false;
      }
    }
  }

  try {
    configApi_->startObserving();
    subscribeToConfigUpdate();
  } catch (...) {
    LOG(ERROR) << "Failed to start config thread";
    return false;
  }

  try {
    spawnAuxiliaryThreads();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    return false;
  }

  startupLock_.notify();

  return true;
}

McrouterInstance* McrouterInstance::createTransient(
  const McrouterOptions& options) {

  auto router = create(options);
  if (router != nullptr) {
    router->isTransient_ = true;
  }
  return router;
}

void McrouterInstance::freeAllMcrouters() {
  if (auto manager = gMcrouterManager.get_weak_fast().lock()) {
    manager->freeAllMcrouters();
  }
}

void McrouterInstance::addStartupOpts(
  std::unordered_map<std::string, std::string> additionalOpts) {
  additionalStartupOpts_.insert(additionalOpts.begin(), additionalOpts.end());
}

std::unordered_map<std::string, std::string>
McrouterInstance::getStartupOpts() const {
  auto result = opts_.toDict();
  result.insert(additionalStartupOpts_.begin(), additionalStartupOpts_.end());
  return result;
}

proxy_t* McrouterInstance::getProxy(size_t index) const {
  if (!proxies_.empty()) {
    assert(proxyThreads_.empty());
    return index < proxies_.size() ? proxies_[index].get() : nullptr;
  } else {
    assert(proxies_.empty());
    return index < proxyThreads_.size() ?
                   &proxyThreads_[index]->proxy() : nullptr;
  }
}

std::string McrouterInstance::routerName() const {
  return "libmcrouter." + opts_.service_name + "." + opts_.router_name;
}

std::unordered_map<std::string, std::pair<bool, size_t>>
McrouterInstance::getSuspectServers() {
  std::unordered_map<std::string, std::pair<bool, size_t>> result;
  pclientOwner_.foreach_shared_synchronized(
    [&result](const std::string& key, ProxyClientShared& shared) {
      auto failureCount = shared.tko.consecutiveFailureCount();
      if (failureCount > 0) {
        result.emplace(key, std::make_pair(shared.tko.isTko(), failureCount));
      }
    });
  return result;
}

void McrouterInstance::onClientDestroyed() {
  if (isTransient_ && liveClients_ > 0) {
    std::thread shutdown_thread{
      [this]() {
        this->tearDown();
      }};

    shutdown_thread.detach();
  }
}

void McrouterInstance::tearDown() {
  // Mark all the clients still attached to this router as zombie
  {
    std::lock_guard<std::mutex> guard(clientListLock_);
    for (auto& client : clientList_) {
      client.isZombie_ = true;
    }
  }

  // unsubscribe from config update
  configUpdateHandle_.reset();
  configApi_->stopObserving(pid_);

  shutdownAndJoinAuxiliaryThreads();

  for (auto& pt : proxyThreads_) {
    pt->stopAndJoin();
  }
  if (onDestroyProxy_) {
    for (size_t i = 0; i < proxies_.size(); ++i) {
      onDestroyProxy_(i, proxies_[i].release());
    }
  }

  proxies_.clear();
  proxyThreads_.clear();

  delete this;
}

McrouterInstance::McrouterInstance(const McrouterOptions& input_options) :
    opts_(options::substituteTemplates(input_options)),
    pid_(getpid()),
    configApi_(createConfigApi(opts_)),
    startupLock_(opts_.num_proxies + 1),
    asyncWriter_(folly::make_unique<AsyncWriter>()),
    statsLogWriter_(folly::make_unique<AsyncWriter>(
                      opts_.stats_async_queue_length)) {
  fb_timer_set_cycle_timer_func(
    []() -> uint64_t { return nowUs(); },
    1.0);
}

/* Needed here for forward declared unique_ptr destruction */
McrouterInstance::~McrouterInstance() {
}

void McrouterInstance::subscribeToConfigUpdate() {
  configUpdateHandle_ = configApi_->subscribe([this]() {
      // we need to wait until all proxies have event base attached
      startupLock_.wait();

      if (reconfigure()) {
        onReconfigureSuccess_.notify();
      } else {
        LOG(ERROR) << "Error while reconfiguring mcrouter after config change";
      }
    });
}

void McrouterInstance::spawnAuxiliaryThreads() {
  startAwriterThreads();
  startObservingRuntimeVarsFile();
  spawnStatUpdaterThread();
  spawnStatLoggerThread();
}

void McrouterInstance::startAwriterThreads() {
  if (!opts_.asynclog_disable) {
    if (!asyncWriter_->start("mcrtr-awriter")) {
      throw std::runtime_error("failed to spawn mcrouter awriter thread");
    }
  }

  if (!statsLogWriter_->start("mcrtr-statsw")) {
    throw std::runtime_error("failed to spawn mcrouter stats writer thread");
  }
}

void McrouterInstance::startObservingRuntimeVarsFile() {
  boost::system::error_code ec;
  if (opts_.runtime_vars_file.empty() ||
      !boost::filesystem::exists(opts_.runtime_vars_file, ec)) {
    return;
  }

  auto& rtVarsDataRef = rtVarsData_;
  auto onUpdate = [&rtVarsDataRef](std::string data) {
    rtVarsDataRef.set(std::make_shared<const RuntimeVarsData>(std::move(data)));
  };

  FileObserver::startObserving(
    opts_.runtime_vars_file,
    taskScheduler_,
    opts_.file_observer_poll_period_ms,
    opts_.file_observer_sleep_before_update_ms,
    std::move(onUpdate)
  );
}

void* McrouterInstance::statUpdaterThreadRun(void* arg) {
  auto router = reinterpret_cast<McrouterInstance*>(arg);
  if (router->opts_.num_proxies == 0) {
    return nullptr;
  }

  // the idx of the oldest bin
  int idx = 0;
  static const int BIN_NUM = (MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
                            MOVING_AVERAGE_BIN_SIZE_IN_SECOND);

  while (true) {
    {
      /* Wait for the full timeout unless shutdown is started */
      std::unique_lock<std::mutex> lock(router->statUpdaterCvMutex_);
      if (router->statUpdaterCv_.wait_for(
            lock,
            std::chrono::seconds(MOVING_AVERAGE_BIN_SIZE_IN_SECOND),
            [router]() { return router->shutdownStarted(); })) {
        /* Shutdown was initiated, so we stop this thread */
        break;
      }
    }

    // to avoid inconsistence among proxies, we lock all mutexes together
    for (size_t i = 0; i < router->opts_.num_proxies; ++i) {
      router->getProxy(i)->stats_lock.lock();
    }

    for (size_t i = 0; i < router->opts_.num_proxies; ++i) {
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

    for (size_t i = 0; i < router->opts_.num_proxies; ++i) {
      router->getProxy(i)->stats_lock.unlock();
    }

    idx = (idx + 1) % BIN_NUM;
  }
  return nullptr;
}

void McrouterInstance::spawnStatUpdaterThread() {
  auto rc = spawnThread(&statUpdaterThreadHandle_,
                        &statUpdaterThreadStack_,
                        &McrouterInstance::statUpdaterThreadRun,
                        this, wantRealtimeThreads());
  if (!rc) {
    throw std::runtime_error("failed to spawn mcrouter stat updater thread");
  }

  mcrouterSetThreadName(statUpdaterThreadHandle_, opts_, "stats");
}

void McrouterInstance::spawnStatLoggerThread() {
  mcrouterLogger_ = createMcrouterLogger(this);
  mcrouterLogger_->start();
}

void McrouterInstance::shutdownAndJoinAuxiliaryThreads() {
  shutdownLock_.shutdownOnce(
    [this]() {
      statUpdaterCv_.notify_all();
      joinAuxiliaryThreads();
    });
}

void McrouterInstance::joinAuxiliaryThreads() {
  /* pid check is a huge hack to make PHP fork() kinda sorta work.
     After fork(), the child doesn't have the thread but does have
     the full copy of the stack which we must cleanup. */
  if (getpid() == pid_) {
    taskScheduler_.shutdownAllTasks();
    if (statUpdaterThreadHandle_) {
      pthread_join(statUpdaterThreadHandle_, nullptr);
    }
  } else {
    taskScheduler_.forkWorkAround();
  }

  if (mcrouterLogger_) {
    mcrouterLogger_->stop();
  }

  if (statUpdaterThreadStack_) {
    free(statUpdaterThreadStack_);
    statUpdaterThreadStack_ = nullptr;
  }

  stopAwriterThreads();
}

void McrouterInstance::stopAwriterThreads() {
  asyncWriter_->stop();
  statsLogWriter_->stop();
}

bool McrouterInstance::reconfigure() {
  bool success = false;

  {
    std::lock_guard<std::mutex> lg(configReconfigLock_);
    /* mark config attempt before, so that
       successful config is always >= last config attempt. */
    lastConfigAttempt_ = time(nullptr);

    configApi_->trackConfigSources();
    std::string config;
    success = configApi_->getConfigFile(config);
    if (success) {
      success = router_configure_from_string(this, config);
    } else {
      logFailure(this, failure::Category::kBadEnvironment,
                 "Can not read config file");
    }

    if (!success) {
      configFailures_++;
      configApi_->abandonTrackedSources();
    } else {
      configApi_->subscribeToTrackedSources();
    }
  }

  return success;
}

bool McrouterInstance::configure(folly::StringPiece input) {
  std::vector<std::shared_ptr<ProxyConfig>> newConfigs;
  try {
    // assume default_route, default_region and default_cluster are same for
    // each proxy
    ProxyConfigBuilder builder(
      opts_,
      configApi_.get(),
      input);

    for (size_t i = 0; i < opts_.num_proxies; i++) {
      newConfigs.push_back(builder.buildConfig(getProxy(i)));
    }
  } catch (const std::exception& e) {
    logFailure(this, failure::Category::kInvalidConfig,
               "Failed to reconfigure: {}", e.what());
    return false;
  }

  for (size_t i = 0; i < opts_.num_proxies; i++) {
    proxy_config_swap(getProxy(i), newConfigs[i]);
  }

  VLOG_IF(0, !opts_.constantly_reload_configs) <<
      "reconfigured " << opts_.num_proxies << " proxies with " <<
      newConfigs[0]->getClients().size() << " clients (" <<
      newConfigs[0]->getConfigMd5Digest() << ")";

  return true;
}

}}} // facebook::memcache::mcrouter
