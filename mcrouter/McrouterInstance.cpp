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
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/json.h>
#include <folly/MapUtil.h>
#include <folly/Singleton.h>

#include "mcrouter/awriter.h"
#include "mcrouter/FileObserver.h"
#include "mcrouter/lib/cycles/Cycles.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/stats.h"
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

  McrouterInstance* mcrouterGetCreate(
    folly::StringPiece persistence_id,
    const McrouterOptions& options,
    const std::vector<folly::EventBase*>& evbs) {

    std::lock_guard<std::mutex> lg(mutex_);

    auto mcrouter = folly::get_default(mcrouters_, persistence_id.str(),
                                       nullptr);
    if (!mcrouter) {
      mcrouter = McrouterInstance::create(options.clone(), evbs);
      if (mcrouter) {
        mcrouters_[persistence_id.str()] = mcrouter;
      }
    }
    return mcrouter.get();
  }

  McrouterInstance* mcrouterGet(folly::StringPiece persistence_id) {
    std::lock_guard<std::mutex> lg(mutex_);

    return folly::get_default(mcrouters_, persistence_id.str(), nullptr).get();
  }

  void freeAllMcrouters() {
    std::lock_guard<std::mutex> lg(mutex_);
    mcrouters_.clear();
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<McrouterInstance>> mcrouters_;
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

McrouterInstance* McrouterInstance::init(
  folly::StringPiece persistence_id,
  const McrouterOptions& options,
  const std::vector<folly::EventBase*>& evbs) {

  if (auto manager = gMcrouterManager.get_weak().lock()) {
    return manager->mcrouterGetCreate(persistence_id, options, evbs);
  }

  return nullptr;
}

McrouterInstance* McrouterInstance::get(folly::StringPiece persistence_id) {
  if (auto manager = gMcrouterManager.get_weak().lock()) {
    return manager->mcrouterGet(persistence_id);
  }

  return nullptr;
}

McrouterInstance* McrouterInstance::createRaw(
  McrouterOptions input_options,
  const std::vector<folly::EventBase*>& evbs) {

  extraValidateOptions(input_options);

  if (!isValidRouterName(input_options.service_name) ||
      !isValidRouterName(input_options.router_name)) {
    throw std::runtime_error(
      "Invalid service_name or router_name provided; must be"
      " strings matching [a-zA-Z0-9_-]+");
  }

  if (input_options.test_mode) {
    // test-mode disables all logging.
    LOG(WARNING) << "Running mcrouter in test mode. This mode should not be "
      "used in production.";
    applyTestMode(input_options);
  }

  if (!input_options.async_spool.empty()) {
    auto rc = ::access(input_options.async_spool.c_str(), W_OK);
    PLOG_IF(WARNING, rc) << "Error while checking spooldir (" <<
      input_options.async_spool << ")";
  }

  if (input_options.enable_failure_logging) {
    initFailureLogger();
  }

  auto router = new McrouterInstance(std::move(input_options));

  try {
    folly::json::serialization_opts jsonOpts;
    jsonOpts.sort_keys = true;
    auto dict = folly::toDynamic(router->getStartupOpts());
    auto jsonStr = folly::json::serialize(dict, jsonOpts);
    failure::setServiceContext(routerName(router->opts()),
                               jsonStr.toStdString());

    if (router->spinUp(evbs)) {
      return router;
    }
  } catch (...) {
  }

  delete router;
  return nullptr;
}

std::shared_ptr<McrouterInstance> McrouterInstance::create(
  McrouterOptions input_options,
  const std::vector<folly::EventBase*>& evbs) {

  return folly::fibers::runInMainContext(
    [&] () mutable {
      return std::shared_ptr<McrouterInstance>(
        createRaw(std::move(input_options), evbs),
        /* Custom deleter since ~McrouterInstance() is private */
        [] (McrouterInstance* inst) {
          delete inst;
        }
      );
    });
}

McrouterClient::Pointer McrouterInstance::createClient(
  mcrouter_client_callbacks_t callbacks,
  void* arg,
  size_t max_outstanding) {

  return McrouterClient::create(shared_from_this(),
                                callbacks,
                                arg,
                                max_outstanding,
                                /* sameThread= */ false);
}

McrouterClient::Pointer McrouterInstance::createSameThreadClient(
  mcrouter_client_callbacks_t callbacks,
  void* arg,
  size_t max_outstanding) {

  return McrouterClient::create(shared_from_this(),
                                callbacks,
                                arg,
                                max_outstanding,
                                /* sameThread= */ true);
}

bool McrouterInstance::spinUp(const std::vector<folly::EventBase*>& evbs) {
  CHECK(evbs.empty() || evbs.size() == opts_.num_proxies);

  for (size_t i = 0; i < opts_.num_proxies; i++) {
    try {
      if (evbs.empty()) {
        proxyThreads_.emplace_back(folly::make_unique<ProxyThread>(*this));
      } else {
        CHECK(evbs[i] != nullptr);
        proxies_.emplace_back(proxy_t::createProxy(*this, *evbs[i]));
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

  for (auto& pt : proxyThreads_) {
    try {
      pt->spawn();
    } catch (const std::system_error& e) {
      LOG(ERROR) << "Failed to start proxy thread: " << e.what();
      return false;
    } catch (...) {
      LOG(ERROR) << "Failed to start proxy thread";
      return false;
    }
  }

  try {
    spawnAuxiliaryThreads();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    return false;
  }

  return true;
}

void McrouterInstance::freeAllMcrouters() {
  if (auto manager = gMcrouterManager.get_weak().lock()) {
    manager->freeAllMcrouters();
  }
}

void McrouterInstance::addStartupOpts(
  std::unordered_map<std::string, std::string> additionalOpts) {
  additionalStartupOpts_.insert(additionalOpts.begin(), additionalOpts.end());
}

std::unordered_map<std::string, std::string>
McrouterInstance::getStartupOpts() const {
  const size_t kMaxOptionValueLength = 256;

  auto result = opts_.toDict();
  result.insert(additionalStartupOpts_.begin(), additionalStartupOpts_.end());
  result.emplace("version", MCROUTER_PACKAGE_STRING);
  for (auto& it : result) {
    it.second = shorten(it.second, kMaxOptionValueLength);
  }
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

proxy_t::Pointer McrouterInstance::releaseProxy(size_t index) {
  assert(index < proxies_.size());
  return std::move(proxies_[index]);
}

McrouterInstance::McrouterInstance(McrouterOptions input_options) :
    opts_(std::move(input_options)),
    pid_(getpid()),
    configApi_(createConfigApi(opts_)),
    asyncWriter_(folly::make_unique<AsyncWriter>()),
    statsLogWriter_(folly::make_unique<AsyncWriter>(
                      opts_.stats_async_queue_length)),
    leaseTokenMap_(opts_.enable_lease_pairing
        ? folly::make_unique<LeaseTokenMap>(evbAuxiliaryThread_)
        : nullptr) {
  fb_timer_set_cycle_timer_func(
    []() -> uint64_t { return nowUs(); },
    1.0);
}

void McrouterInstance::shutdownImpl() noexcept {
  joinAuxiliaryThreads();

  for (auto& pt : proxyThreads_) {
    pt->stopAndJoin();
  }
}

void McrouterInstance::shutdown() noexcept {
  CHECK(!shutdownStarted_.exchange(true));
  shutdownImpl();
}

McrouterInstance::~McrouterInstance() {
  if (!shutdownStarted_.exchange(true)) {
    shutdownImpl();
  }
}

void McrouterInstance::subscribeToConfigUpdate() {
  configUpdateHandle_ = configApi_->subscribe([this]() {
      if (reconfigure()) {
        onReconfigureSuccess_.notify();
      } else {
        LOG(ERROR) << "Error while reconfiguring mcrouter after config change";
      }
    });
}

void McrouterInstance::spawnAuxiliaryThreads() {
  configApi_->startObserving();
  subscribeToConfigUpdate();

  startAwriterThreads();
  startObservingRuntimeVarsFile();
  statUpdaterThread_ = std::thread(
    [this] () {
      statUpdaterThreadRun();
    });
  spawnStatLoggerThread();
  if (opts_.cpu_cycles) {
    cycles::startExtracting([this](cycles::CycleStats stats) {
      auto anyProxy = getProxy(0);
      if (anyProxy) {
        stat_set_uint64(anyProxy->stats, cycles_avg_stat, stats.avg);
        stat_set_uint64(anyProxy->stats, cycles_min_stat, stats.min);
        stat_set_uint64(anyProxy->stats, cycles_max_stat, stats.max);
        stat_set_uint64(anyProxy->stats, cycles_p01_stat, stats.p01);
        stat_set_uint64(anyProxy->stats, cycles_p05_stat, stats.p05);
        stat_set_uint64(anyProxy->stats, cycles_p50_stat, stats.p50);
        stat_set_uint64(anyProxy->stats, cycles_p95_stat, stats.p95);
        stat_set_uint64(anyProxy->stats, cycles_p99_stat, stats.p99);
        stat_set_uint64(anyProxy->stats, cycles_num_stat, stats.numSamples);
      }
    });
  }
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

  startObservingFile(
    opts_.runtime_vars_file,
    *evbAuxiliaryThread_.getEventBase(),
    opts_.file_observer_poll_period_ms,
    opts_.file_observer_sleep_before_update_ms,
    std::move(onUpdate)
  );
}

void McrouterInstance::statUpdaterThreadRun() {
  mcrouterSetThisThreadName(opts_, "stats");

  if (opts_.num_proxies == 0) {
    return;
  }

  // the idx of the oldest bin
  int idx = 0;
  static const int BIN_NUM = (MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
                            MOVING_AVERAGE_BIN_SIZE_IN_SECOND);

  while (true) {
    {
      /* Wait for the full timeout unless shutdown is started */
      std::unique_lock<std::mutex> lock(statUpdaterCvMutex_);
      if (statUpdaterCv_.wait_for(
            lock,
            std::chrono::seconds(MOVING_AVERAGE_BIN_SIZE_IN_SECOND),
            [this]() { return shutdownStarted_.load(); })) {
        /* Shutdown was initiated, so we stop this thread */
        break;
      }
    }

    // to avoid inconsistence among proxies, we lock all mutexes together
    for (size_t i = 0; i < opts_.num_proxies; ++i) {
      getProxy(i)->stats_lock.lock();
    }

    for (size_t i = 0; i < opts_.num_proxies; ++i) {
      auto proxy = getProxy(i);
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

    for (size_t i = 0; i < opts_.num_proxies; ++i) {
      getProxy(i)->stats_lock.unlock();
    }

    idx = (idx + 1) % BIN_NUM;
  }
}

void McrouterInstance::spawnStatLoggerThread() {
  mcrouterLogger_ = createMcrouterLogger(*this);
  mcrouterLogger_->start();
}

void McrouterInstance::joinAuxiliaryThreads() noexcept {
  // unsubscribe from config update
  configUpdateHandle_.reset();
  if (configApi_) {
    configApi_->stopObserving(pid_);
  }

  statUpdaterCv_.notify_all();

  /* pid check is a huge hack to make PHP fork() kinda sorta work.
     After fork(), the child doesn't have the thread but does have
     the full copy of the stack which we must cleanup. */
  if (getpid() == pid_) {
    if (statUpdaterThread_.joinable()) {
      statUpdaterThread_.join();
    }
  }

  if (opts_.cpu_cycles) {
    cycles::stopExtracting();
  }

  if (mcrouterLogger_) {
    mcrouterLogger_->stop();
  }

  stopAwriterThreads();

  evbAuxiliaryThread_.stop();
}

void McrouterInstance::stopAwriterThreads() noexcept {
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
      success = configure(config);
    } else {
      MC_LOG_FAILURE(opts(), failure::Category::kBadEnvironment,
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
      configApi(),
      input);

    for (size_t i = 0; i < opts_.num_proxies; i++) {
      newConfigs.push_back(builder.buildConfig(*getProxy(i)));
    }
  } catch (const std::exception& e) {
    MC_LOG_FAILURE(opts(), failure::Category::kInvalidConfig,
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
