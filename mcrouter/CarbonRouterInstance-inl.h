/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <vector>

#include <boost/filesystem/operations.hpp>

#include <folly/DynamicConverter.h>
#include <folly/MapUtil.h>
#include <folly/Singleton.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/AsyncWriter.h"
#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/ThreadUtil.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

bool isValidRouterName(folly::StringPiece name);

class McrouterManager {
 public:
  McrouterManager();

  ~McrouterManager();

  void freeAllMcrouters();

  template <class RouterInfo>
  CarbonRouterInstance<RouterInfo>* mcrouterGetCreate(
      folly::StringPiece persistence_id,
      const McrouterOptions& options,
      const std::vector<folly::EventBase*>& evbs) {
    std::shared_ptr<CarbonRouterInstanceBase> mcrouterBase;

    {
      std::lock_guard<std::mutex> lg(mutex_);
      mcrouterBase = folly::get_default(mcrouters_, persistence_id.str());
    }
    if (!mcrouterBase) {
      std::lock_guard<std::mutex> ilg(initMutex_);
      {
        std::lock_guard<std::mutex> lg(mutex_);
        mcrouterBase = folly::get_default(mcrouters_, persistence_id.str());
      }
      if (!mcrouterBase) {
        std::shared_ptr<CarbonRouterInstance<RouterInfo>> mcrouter =
            CarbonRouterInstance<RouterInfo>::create(options.clone(), evbs);
        if (mcrouter) {
          std::lock_guard<std::mutex> lg(mutex_);
          mcrouters_[persistence_id.str()] = mcrouter;
          return mcrouter.get();
        }
      }
    }
    return dynamic_cast<CarbonRouterInstance<RouterInfo>*>(mcrouterBase.get());
  }

  template <class RouterInfo>
  CarbonRouterInstance<RouterInfo>* mcrouterGet(
      folly::StringPiece persistence_id) {
    std::lock_guard<std::mutex> lg(mutex_);
    auto mcrouterBase =
        folly::get_default(mcrouters_, persistence_id.str(), nullptr).get();
    return dynamic_cast<CarbonRouterInstance<RouterInfo>*>(mcrouterBase);
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<CarbonRouterInstanceBase>>
      mcrouters_;
  // protects mcrouters_
  std::mutex mutex_;
  // initMutex_ must not be taken under mutex_, otherwise deadlock is possible
  std::mutex initMutex_;
};

extern folly::Singleton<McrouterManager> gMcrouterManager;

} // detail

template <class RouterInfo>
/* static  */ CarbonRouterInstance<RouterInfo>*
CarbonRouterInstance<RouterInfo>::init(
    folly::StringPiece persistence_id,
    const McrouterOptions& options,
    const std::vector<folly::EventBase*>& evbs) {
  if (auto manager = detail::gMcrouterManager.try_get()) {
    return manager->mcrouterGetCreate<RouterInfo>(
        persistence_id, options, evbs);
  }

  return nullptr;
}

template <class RouterInfo>
CarbonRouterInstance<RouterInfo>* CarbonRouterInstance<RouterInfo>::get(
    folly::StringPiece persistence_id) {
  if (auto manager = detail::gMcrouterManager.try_get()) {
    return manager->mcrouterGet<RouterInfo>(persistence_id);
  }

  return nullptr;
}

template <class RouterInfo>
CarbonRouterInstance<RouterInfo>* CarbonRouterInstance<RouterInfo>::createRaw(
    McrouterOptions input_options,
    const std::vector<folly::EventBase*>& evbs) {
  extraValidateOptions(input_options);

  if (!detail::isValidRouterName(input_options.service_name) ||
      !detail::isValidRouterName(input_options.router_name)) {
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
    PLOG_IF(WARNING, rc) << "Error while checking spooldir ("
                         << input_options.async_spool << ")";
  }

  if (input_options.enable_failure_logging) {
    initFailureLogger();
  }

  auto router = new CarbonRouterInstance<RouterInfo>(std::move(input_options));

  folly::Expected<folly::Unit, std::string> result;
  try {
    folly::json::serialization_opts jsonOpts;
    jsonOpts.sort_keys = true;
    auto dict = folly::toDynamic(router->getStartupOpts());
    auto jsonStr = folly::json::serialize(dict, jsonOpts);
    failure::setServiceContext(routerName(router->opts()), std::move(jsonStr));

    result = router->spinUp(evbs);
    if (result.hasValue()) {
      return router;
    }
  } catch (...) {
    result = folly::makeUnexpected(
        folly::exceptionStr(std::current_exception()).toStdString());
  }

  result.error() = folly::sformat(
      "mcrouter error (router name '{}', flavor '{}',"
      " service '{}'): {}",
      router->opts().router_name,
      router->opts().flavor_name,
      router->opts().service_name,
      result.error());

  // Proxy destruction depends on EventBase loop running. Ensure that all user
  // EventBases have their loops running and if not - loop them ourselves.
  std::vector<std::pair<folly::EventBase*, std::thread>> tmpThreads;
  for (auto evbPtr : evbs) {
    if (evbPtr->isRunning()) {
      continue;
    }
    tmpThreads.emplace_back(
        evbPtr, std::thread([evbPtr] { evbPtr->loopForever(); }));
  }
  delete router;
  for (auto& tmpThread : tmpThreads) {
    tmpThread.first->terminateLoopSoon();
    tmpThread.second.join();
  }

  throw std::runtime_error(std::move(result.error()));
}

template <class RouterInfo>
std::shared_ptr<CarbonRouterInstance<RouterInfo>>
CarbonRouterInstance<RouterInfo>::create(
    McrouterOptions input_options,
    const std::vector<folly::EventBase*>& evbs) {
  return folly::fibers::runInMainContext([&]() mutable {
    return std::shared_ptr<CarbonRouterInstance<RouterInfo>>(
        createRaw(std::move(input_options), evbs),
        /* Custom deleter since ~CarbonRouterInstance() is private */
        [](CarbonRouterInstance<RouterInfo>* inst) { delete inst; });
  });
}

template <class RouterInfo>
typename CarbonRouterClient<RouterInfo>::Pointer
CarbonRouterInstance<RouterInfo>::createClient(
    size_t max_outstanding,
    bool max_outstanding_error) {
  return CarbonRouterClient<RouterInfo>::create(
      this->shared_from_this(),
      max_outstanding,
      max_outstanding_error,
      /* sameThread= */ false);
}

template <class RouterInfo>
typename CarbonRouterClient<RouterInfo>::Pointer
CarbonRouterInstance<RouterInfo>::createSameThreadClient(
    size_t max_outstanding) {
  return CarbonRouterClient<RouterInfo>::create(
      this->shared_from_this(),
      max_outstanding,
      /* maxOutstandingError= */ true,
      /* sameThread= */ true);
}

template <class RouterInfo>
folly::Expected<folly::Unit, std::string>
CarbonRouterInstance<RouterInfo>::spinUp(
    const std::vector<folly::EventBase*>& evbs) {
  CHECK(evbs.empty() || evbs.size() == opts_.num_proxies);

  // Must init compression before creating proxies.
  if (opts_.enable_compression) {
    initCompression(*this);
  }

  bool configuringFromDisk = false;
  {
    std::lock_guard<std::mutex> lg(configReconfigLock_);

    auto builder = createConfigBuilder();
    if (builder.hasError()) {
      std::string initialError = std::move(builder.error());
      // If we cannot create ConfigBuilder from normal config,
      // try creating it from backup files.
      configApi_->enableReadingFromBackupFiles();
      configuringFromDisk = true;
      builder = createConfigBuilder();
      if (builder.hasError()) {
        return folly::makeUnexpected(folly::sformat(
            "Failed to configure, initial error '{}', from backup '{}'",
            initialError,
            builder.error()));
      }
    }

    for (size_t i = 0; i < opts_.num_proxies; i++) {
      if (evbs.empty()) {
        try {
          proxyThreads_.emplace_back(std::make_unique<ProxyThread>(*this, i));
        } catch (...) {
          return folly::makeUnexpected(folly::sformat(
              "Failed to start proxy thread: {}",
              folly::exceptionStr(std::current_exception())));
        }
        proxyEvbs_.push_back(std::make_unique<folly::VirtualEventBase>(
            proxyThreads_.back()->getEventBase()));
      } else {
        CHECK(evbs[i] != nullptr);
        proxyEvbs_.push_back(
            std::make_unique<folly::VirtualEventBase>(*evbs[i]));
      }

      try {
        proxies_.emplace_back(
            Proxy<RouterInfo>::createProxy(*this, *proxyEvbs_[i], i));
      } catch (...) {
        return folly::makeUnexpected(folly::sformat(
            "Failed to create proxy: {}",
            folly::exceptionStr(std::current_exception())));
      }
    }

    auto configResult = configure(builder.value());
    if (configResult.hasValue()) {
      configApi_->subscribeToTrackedSources();
    } else {
      configFailures_++;
      configApi_->abandonTrackedSources();

      // If we successfully created ConfigBuilder from normal config, but
      // failed to configure, we have to create ConfigBuilder again,
      // this time reading from backup files.
      configApi_->enableReadingFromBackupFiles();
      configuringFromDisk = true;
      builder = createConfigBuilder();
      auto reconfigResult = configure(builder.value());
      if (reconfigResult.hasValue()) {
        configApi_->subscribeToTrackedSources();
      } else {
        configApi_->abandonTrackedSources();
        LOG(ERROR) << "Failed to configure proxies";
        return folly::makeUnexpected(folly::sformat(
            "Failed to configure, initial error '{}', from backup '{}'",
            configResult.error(),
            reconfigResult.error()));
      }
    }
  }

  configuredFromDisk_ = configuringFromDisk;

  startTime_ = time(nullptr);

  spawnAuxiliaryThreads();

  return folly::Unit();
}

template <class RouterInfo>
Proxy<RouterInfo>* CarbonRouterInstance<RouterInfo>::getProxy(
    size_t index) const {
  return index < proxies_.size() ? proxies_[index] : nullptr;
}

template <class RouterInfo>
ProxyBase* CarbonRouterInstance<RouterInfo>::getProxyBase(size_t index) const {
  return getProxy(index);
}

template <class RouterInfo>
CarbonRouterInstance<RouterInfo>::CarbonRouterInstance(
    McrouterOptions inputOptions)
    : CarbonRouterInstanceBase(std::move(inputOptions)) {}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::shutdownImpl() noexcept {
  joinAuxiliaryThreads();
  // Join all proxy threads
  proxyEvbs_.clear();
  for (auto& pt : proxyThreads_) {
    pt->stopAndJoin();
  }
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::shutdown() noexcept {
  CHECK(!shutdownStarted_.exchange(true));
  shutdownImpl();
}

template <class RouterInfo>
CarbonRouterInstance<RouterInfo>::~CarbonRouterInstance() {
  if (!shutdownStarted_.exchange(true)) {
    shutdownImpl();
  }
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::subscribeToConfigUpdate() {
  configUpdateHandle_ = configApi_->subscribe([this]() {
    bool success = false;
    {
      std::lock_guard<std::mutex> lg(configReconfigLock_);

      auto builder = createConfigBuilder();
      if (builder) {
        success = reconfigure(builder.value());
      }
    }
    if (success) {
      configuredFromDisk_ = false;
      onReconfigureSuccess_.notify();
    } else {
      LOG(ERROR) << "Error while reconfiguring mcrouter after config change";
    }
  });
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::spawnAuxiliaryThreads() {
  configApi_->startObserving();
  subscribeToConfigUpdate();

  startObservingRuntimeVarsFile();
  registerOnUpdateCallbackForRxmits();
  registerForStatsUpdates();
  spawnStatLoggerThread();
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::startObservingRuntimeVarsFile() {
  if (opts_.runtime_vars_file.empty()) {
    return;
  }

  auto onUpdate = [rtVarsDataWeak = rtVarsDataWeak()](std::string data) {
    if (auto rtVarsDataPtr = rtVarsDataWeak.lock()) {
      rtVarsDataPtr->set(
          std::make_shared<const RuntimeVarsData>(std::move(data)));
    }
  };

  rtVarsDataObserver_ =
      startObservingRuntimeVarsFileCustom(opts_.runtime_vars_file, onUpdate);

  if (rtVarsDataObserver_) {
    return;
  }

  boost::system::error_code ec;
  if (!boost::filesystem::exists(opts_.runtime_vars_file, ec)) {
    return;
  }

  if (auto scheduler = functionScheduler()) {
    runtimeVarsObserverHandle_ = startObservingFile(
        opts_.runtime_vars_file,
        scheduler,
        std::chrono::milliseconds(opts_.file_observer_poll_period_ms),
        std::chrono::milliseconds(opts_.file_observer_sleep_before_update_ms),
        std::move(onUpdate));
  } else {
    MC_LOG_FAILURE(
        opts(),
        failure::Category::kSystemError,
        "Global function scheduler not available");
  }
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::spawnStatLoggerThread() {
  mcrouterLogger_ = createMcrouterLogger(*this);
  mcrouterLogger_->start();
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::joinAuxiliaryThreads() noexcept {
  // unsubscribe from config update
  configUpdateHandle_.reset();
  if (configApi_) {
    configApi_->stopObserving(pid_);
  }

  deregisterForStatsUpdates();

  if (mcrouterLogger_) {
    mcrouterLogger_->stop();
  }

  runtimeVarsObserverHandle_.reset();
}

template <class RouterInfo>
bool CarbonRouterInstance<RouterInfo>::reconfigure(
    const ProxyConfigBuilder& builder) {
  auto result = configure(builder);

  if (result.hasError()) {
    configFailures_++;
    configApi_->abandonTrackedSources();
  } else {
    configApi_->subscribeToTrackedSources();
  }

  return result.hasValue();
}

template <class RouterInfo>
folly::Expected<folly::Unit, std::string>
CarbonRouterInstance<RouterInfo>::configure(const ProxyConfigBuilder& builder) {
  VLOG_IF(0, !opts_.constantly_reload_configs) << "started reconfiguring";
  std::vector<std::shared_ptr<ProxyConfig<RouterInfo>>> newConfigs;
  try {
    for (size_t i = 0; i < opts_.num_proxies; i++) {
      newConfigs.push_back(builder.buildConfig<RouterInfo>(*getProxy(i)));
    }
  } catch (const std::exception& e) {
    std::string error = folly::sformat("Failed to reconfigure: {}", e.what());
    MC_LOG_FAILURE(opts(), failure::Category::kInvalidConfig, error);

    return folly::makeUnexpected(std::move(error));
  }

  for (size_t i = 0; i < opts_.num_proxies; i++) {
    proxy_config_swap(getProxy(i), newConfigs[i]);
  }

  VLOG_IF(0, !opts_.constantly_reload_configs)
      << "reconfigured " << opts_.num_proxies << " proxies with "
      << newConfigs[0]->getPools().size() << " pools, "
      << newConfigs[0]->calcNumClients() << " clients "
      << newConfigs[0]->getConfigMd5Digest() << ")";

  return folly::Unit();
}

template <class RouterInfo>
folly::Expected<ProxyConfigBuilder, std::string>
CarbonRouterInstance<RouterInfo>::createConfigBuilder() {
  /* mark config attempt before, so that
     successful config is always >= last config attempt. */
  lastConfigAttempt_ = time(nullptr);
  configApi_->trackConfigSources();
  std::string config;
  std::string path;
  std::string error;
  if (configApi_->getConfigFile(config, path)) {
    try {
      // assume default_route, default_region and default_cluster are same for
      // each proxy
      return ProxyConfigBuilder(opts_, configApi(), config);
    } catch (const std::exception& e) {
      MC_LOG_FAILURE(
          opts(),
          failure::Category::kInvalidConfig,
          "Failed to reconfigure: {}",
          e.what());
      error = e.what();
    }
  }
  MC_LOG_FAILURE(
      opts(),
      failure::Category::kBadEnvironment,
      "Can not read config from {}",
      path);
  configFailures_++;
  configApi_->abandonTrackedSources();
  return folly::makeUnexpected(std::move(error));
}

template <class RouterInfo>
void CarbonRouterInstance<RouterInfo>::registerOnUpdateCallbackForRxmits() {
  rxmitHandle_ = rtVarsData().subscribeAndCall([this](
      std::shared_ptr<const RuntimeVarsData> /* oldVars */,
      std::shared_ptr<const RuntimeVarsData> newVars) {
    if (!newVars) {
      return;
    }
    const auto val = newVars->getVariableByName("disable_rxmit_reconnection");
    if (val != nullptr) {
      checkLogic(
          val.isBool(),
          "runtime vars 'disable_rxmit_reconnection' is not a boolean");
      disableRxmitReconnection_ = val.asBool();
    }
  });
}

template <class RouterInfo>
/* static */ void CarbonRouterInstance<RouterInfo>::freeAllMcrouters() {
  freeAllRouters();
}

} // mcrouter
} // memcache
} // facebook
