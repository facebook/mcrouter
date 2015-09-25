/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "proxy.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>

#include <boost/regex.hpp>

#include <folly/DynamicConverter.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Random.h>
#include <folly/Range.h>
#include <folly/ThreadName.h>
#include <folly/File.h>
#include <folly/experimental/fibers/EventBaseLoopController.h>

#include "mcrouter/async.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/cycles/Cycles.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/MessageQueue.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShardSplitter.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

folly::fibers::FiberManager::Options getFiberManagerOptions(
    const McrouterOptions& opts) {
  folly::fibers::FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.recordStackEvery = opts.fibers_record_stack_size_every;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  fmOpts.useGuardPages = opts.fibers_use_guard_pages;
  return fmOpts;
}

}  // anonymous namespace


proxy_t::proxy_t(McrouterInstance& rtr)
    : router_(rtr),
      destinationMap(folly::make_unique<ProxyDestinationMap>(this)),
      randomGenerator(folly::randomNumberSeed()),
      fiberManager(
        fiber_local::ContextTypeTag(),
        folly::make_unique<folly::fibers::EventBaseLoopController>(),
        getFiberManagerOptions(router_.opts())) {

  memset(stats, 0, sizeof(stats));
  memset(stats_bin, 0, sizeof(stats_bin));
  memset(stats_num_within_window, 0, sizeof(stats_num_within_window));

  static uint64_t next_magic = 0x12345678900000LL;

  magic = __sync_fetch_and_add(&next_magic, 1);

  init_stats(stats);

  messageQueue_ = folly::make_unique<MessageQueue<ProxyMessage>>(
    router_.opts().client_queue_size,
    [this] (ProxyMessage&& message) {
      this->messageReady(message.type, message.data);
    },
    router_.opts().client_queue_no_notify_rate,
    router_.opts().client_queue_wait_threshold_us,
    &nowUs,
    [this] () {
      stat_incr_safe(stats, client_queue_notifications_stat);
    }
  );

  statsContainer = folly::make_unique<ProxyStatsContainer>(*this);
}

proxy_t::Pointer proxy_t::createProxy(McrouterInstance& router,
                                      folly::EventBase& eventBase) {
  /* This hack is needed to make sure proxy_t stays alive
     until at least event base managed to run the callback below */
  auto proxy = std::shared_ptr<proxy_t>(new proxy_t(router));
  proxy->self_ = proxy;

  eventBase.runInEventBaseThread(
    [proxy, &eventBase] () {
      proxy->eventBase_ = &eventBase;
      proxy->messageQueue_->attachEventBase(eventBase);

      dynamic_cast<folly::fibers::EventBaseLoopController&>(
        proxy->fiberManager.loopController()).attachEventBase(eventBase);

      std::chrono::milliseconds connectionResetInterval{
        proxy->router_.opts().reset_inactive_connection_interval
      };

      if (connectionResetInterval.count() > 0) {
        proxy->destinationMap->setResetTimer(connectionResetInterval);
      }

      if (proxy->router_.opts().cpu_cycles) {
        cycles::attachEventBase(eventBase);
        proxy->fiberManager.setObserver(&proxy->cyclesObserver);
      }
    });

  return Pointer(proxy.get());
}

std::shared_ptr<ProxyConfig> proxy_t::getConfig() const {
  std::lock_guard<SFRReadLock> lg(
    const_cast<SFRLock&>(configLock_).readLock());
  return config_;
}

std::pair<std::unique_lock<SFRReadLock>, ProxyConfig&>
proxy_t::getConfigLocked() const {
  std::unique_lock<SFRReadLock> lock(
    const_cast<SFRLock&>(configLock_).readLock());
  /* make_pair strips the reference, so construct directly */
  return std::pair<std::unique_lock<SFRReadLock>, ProxyConfig&>(
    std::move(lock), *config_);
}

std::shared_ptr<ProxyConfig> proxy_t::swapConfig(
  std::shared_ptr<ProxyConfig> newConfig) {

  std::lock_guard<SFRWriteLock> lg(configLock_.writeLock());
  auto old = std::move(config_);
  config_ = std::move(newConfig);
  return old;
}

/** drain and delete proxy object */
proxy_t::~proxy_t() {
  destinationMap.reset();

  being_destroyed = true;

  if (messageQueue_) {
    messageQueue_->drain();
  }

  magic = 0xdeadbeefdeadbeefLL;
}

void proxy_t::sendMessage(ProxyMessage::Type t, void* data) noexcept {
  CHECK(messageQueue_.get());
  messageQueue_->blockingWrite(t, data);
}

void proxy_t::drainMessageQueue() {
  CHECK(messageQueue_.get());
  messageQueue_->drain();
}

size_t proxy_t::queueNotifyPeriod() const {
  if (messageQueue_) {
    return messageQueue_->currentNotifyPeriod();
  }
  return 0;
}

void proxy_t::messageReady(ProxyMessage::Type t, void* data) {
  switch (t) {
    case ProxyMessage::Type::REQUEST:
    {
      auto preq = reinterpret_cast<ProxyRequestContext*>(data);
      preq->startProcessing();
    }
    break;

    case ProxyMessage::Type::OLD_CONFIG:
    {
      auto oldConfig = reinterpret_cast<old_config_req_t*>(data);
      delete oldConfig;
    }
    break;

    case ProxyMessage::Type::SHUTDOWN:
      /*
       * No-op. We just wanted to wake this event base up so that
       * it can exit event loop and check router->shutdown
       */
      break;
  }
}

void proxy_t::pump() {
  auto numPriorities = static_cast<int>(ProxyRequestPriority::kNumPriorities);
  for (int i = 0; i < numPriorities; ++i) {
    auto& queue = waitingRequests_[i];
    while (numRequestsProcessing_ < router_.opts().proxy_max_inflight_requests
           && !queue.empty()) {
      --numRequestsWaiting_;
      auto w = queue.popFront();
      stat_decr(stats, proxy_reqs_waiting_stat, 1);

      w->process(this);
    }
  }
}

uint64_t proxy_t::nextRequestId() {
  return ++nextReqId_;
}

const McrouterOptions& proxy_t::getRouterOptions() const {
  return router_.opts();
}

std::shared_ptr<ShadowSettings>
ShadowSettings::create(const folly::dynamic& json, McrouterInstance& router) {
  auto result = std::shared_ptr<ShadowSettings>(new ShadowSettings());
  try {
    checkLogic(json.isObject(), "json is not an object");
    if (auto jKeyFractionRange = json.get_ptr("key_fraction_range")) {
      checkLogic(jKeyFractionRange->isArray(),
                 "key_fraction_range is not an array");
      auto ar = folly::convertTo<std::vector<double>>(*jKeyFractionRange);
      checkLogic(ar.size() == 2, "key_fraction_range size is not 2");
      result->setKeyRange(ar[0], ar[1]);
    }
    if (auto jIndexRange = json.get_ptr("index_range")) {
      checkLogic(jIndexRange->isArray(), "index_range is not an array");
      auto ar = folly::convertTo<std::vector<size_t>>(*jIndexRange);
      checkLogic(ar.size() == 2, "index_range size is not 2");
      checkLogic(ar[0] <= ar[1], "index_range start > end");
      result->startIndex_ = ar[0];
      result->endIndex_ = ar[1];
    }
    if (auto jKeyFractionRangeRv = json.get_ptr("key_fraction_range_rv")) {
      checkLogic(jKeyFractionRangeRv->isString(),
                 "key_fraction_range_rv is not a string");
      result->keyFractionRangeRv_ = jKeyFractionRangeRv->stringPiece().str();
    }
    if (auto jValidateReplies = json.get_ptr("validate_replies")) {
      checkLogic(jValidateReplies->isBool(),
                 "validate_replies is not a bool");
      result->validateReplies_ = jValidateReplies->getBool();
    }
  } catch (const std::logic_error& e) {
    MC_LOG_FAILURE(router.opts(), failure::Category::kInvalidConfig,
                   "ShadowSettings: {}", e.what());
    return nullptr;
  }

  result->registerOnUpdateCallback(router);

  return result;
}

void ShadowSettings::setKeyRange(double start, double end) {
  checkLogic(0 <= start && start <= end && end <= 1,
             "invalid key_fraction_range [{}, {}]", start, end);
  uint64_t keyStart = start * std::numeric_limits<uint32_t>::max();
  uint64_t keyEnd = end * std::numeric_limits<uint32_t>::max();
  keyRange_ = (keyStart << 32UL) | keyEnd;
}

void ShadowSettings::setValidateReplies(bool validateReplies) {
  validateReplies_ = validateReplies;
}

ShadowSettings::~ShadowSettings() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

void ShadowSettings::registerOnUpdateCallback(McrouterInstance& router) {
  handle_ = router.rtVarsData().subscribeAndCall(
    [this](std::shared_ptr<const RuntimeVarsData> oldVars,
           std::shared_ptr<const RuntimeVarsData> newVars) {
      if (!newVars || keyFractionRangeRv_.empty()) {
        return;
      }
      auto val = newVars->getVariableByName(keyFractionRangeRv_);
      if (val != nullptr) {
        checkLogic(val.isArray(),
                   "runtime vars: {} is not an array", keyFractionRangeRv_);
        checkLogic(val.size() == 2,
                   "runtime vars: size of {} is not 2", keyFractionRangeRv_);
        checkLogic(val[0].isNumber(),
                   "runtime vars: {}#0 is not a number", keyFractionRangeRv_);
        checkLogic(val[1].isNumber(),
                   "runtime vars: {}#1 is not a number", keyFractionRangeRv_);
        setKeyRange(val[0].asDouble(), val[1].asDouble());
      }
    });
}

void proxy_config_swap(proxy_t* proxy,
                       std::shared_ptr<ProxyConfig> config) {
  /* Update the number of server stat for this proxy. */
  stat_set_uint64(proxy->stats, num_servers_stat, config->getClients().size());

  auto oldConfig = proxy->swapConfig(std::move(config));
  stat_set_uint64(proxy->stats, config_last_success_stat, time(nullptr));

  if (oldConfig) {
    auto configReq = new old_config_req_t(std::move(oldConfig));
    proxy->sendMessage(ProxyMessage::Type::OLD_CONFIG, configReq);
  }
}

}}} // facebook::memcache::mcrouter
