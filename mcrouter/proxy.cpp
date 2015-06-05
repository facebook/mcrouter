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
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/priorities.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShardSplitter.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

folly::fibers::FiberManager::Options getFiberManagerOptions(
    const McrouterOptions& opts) {
  folly::fibers::FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.recordStackEvery = opts.fibers_record_stack_size_every;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  return fmOpts;
}

/**
 * @return true  If precheck finds an interesting request and has the reply
 *   set up otherwise this request needs to go through normal flow.
 */
bool precheckRequest(ProxyRequestContext& preq) {
  switch (preq.origReq()->op) {
    // Return error (pretend to not even understand the protocol)
    case mc_op_shutdown:
      preq.sendReply(McReply(mc_res_bad_command));
      break;

    // Return 'Not supported' message
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_flushre:
      preq.sendReply(McReply(mc_res_local_error, "Command not supported"));
      break;

    case mc_op_flushall:
      if (!preq.proxy().opts.enable_flush_cmd) {
        preq.sendReply(McReply(mc_res_local_error, "Command disabled"));
        break;
      }
      /* fallthrough */

    // Everything else is supported
    default:
      auto err = mc_client_req_check(preq.origReq().get());
      if (err != mc_req_err_valid) {
        preq.sendReply(McReply(mc_res_local_error, mc_req_err_to_string(err)));
        break;
      }
      return false;
  }
  return true;
}

}  // anonymous namespace

proxy_t::proxy_t(McrouterInstance& router_, folly::EventBase* eventBase_)
    : router(&router_),
      opts(router->opts()),
      eventBase(eventBase_),
      destinationMap(folly::make_unique<ProxyDestinationMap>(this)),
      durationUs(kExponentialFactor),
      randomGenerator(folly::randomNumberSeed()),
      fiberManager(
        fiber_local::ContextTypeTag(),
        folly::make_unique<folly::fibers::EventBaseLoopController>(),
        getFiberManagerOptions(opts)) {
  memset(stats, 0, sizeof(stats));
  memset(stats_bin, 0, sizeof(stats_bin));
  memset(stats_num_within_window, 0, sizeof(stats_num_within_window));

  static uint64_t next_magic = 0x12345678900000LL;

  magic = __sync_fetch_and_add(&next_magic, 1);

  init_stats(stats);

  if (eventBase != nullptr) {
    onEventBaseAttached();
  }
}

void proxy_t::attachEventBase(folly::EventBase* eventBase_) {
  assert(eventBase == nullptr);
  assert(eventBase_ != nullptr);
  eventBase = eventBase_;
  onEventBaseAttached();
}

void proxy_t::onEventBaseAttached() {
  dynamic_cast<folly::fibers::EventBaseLoopController&>(
    fiberManager.loopController()).attachEventBase(*eventBase);

  init_proxy_event_priorities(this);

  std::chrono::milliseconds connectionResetInterval{
    opts.reset_inactive_connection_interval
  };
  if (connectionResetInterval.count() > 0) {
    destinationMap->setResetTimer(connectionResetInterval);
  }

  messageQueue_ = folly::make_unique<MessageQueue<ProxyMessage>>(
    opts.client_queue_size,
    [this] (ProxyMessage&& message) {
      this->messageReady(message.type, message.data);
    },
    *eventBase,
    opts.client_queue_no_notify_rate,
    opts.client_queue_wait_threshold_us,
    &nowUs,
    [this] () {
      stat_incr_safe(stats, client_queue_notifications_stat);
    }
  );

  statsContainer = folly::make_unique<ProxyStatsContainer>(this);

  if (router != nullptr) {
    router->startupLock().notify();
  }

  if (opts.cpu_cycles) {
    eventBase->runInEventBaseThread([this] {
      cycles::attachEventBase(*this->eventBase);
      this->fiberManager.setObserver(&this->cyclesObserver);
    });
  }
}

std::shared_ptr<ProxyConfigIf> proxy_t::getConfig() const {
  std::lock_guard<SFRReadLock> lg(
    const_cast<SFRLock&>(configLock_).readLock());
  return config_;
}

std::pair<std::unique_lock<SFRReadLock>, ProxyConfigIf&>
proxy_t::getConfigLocked() const {
  std::unique_lock<SFRReadLock> lock(
    const_cast<SFRLock&>(configLock_).readLock());
  /* make_pair strips the reference, so construct directly */
  return std::pair<std::unique_lock<SFRReadLock>, ProxyConfigIf&>(
    std::move(lock), *config_);
}

std::shared_ptr<ProxyConfigIf> proxy_t::swapConfig(
  std::shared_ptr<ProxyConfigIf> newConfig) {

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

void proxy_t::sendMessage(ProxyMessage::Type t, void* data) {
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
      auto preq =
        std::unique_ptr<ProxyRequestContext>(
          reinterpret_cast<ProxyRequestContext*>(data));
      auto client = preq->requester_;

      client->numPending_++;

      if (precheckRequest(*preq)) {
        return;
      }

      if (being_destroyed) {
        /* We can't process this, since 1) we destroyed the config already,
           and 2) the clients are winding down, so we wouldn't get any
           meaningful response back anyway. */
        LOG(ERROR) << "Outstanding request on a proxy that's being destroyed";
        preq->sendReply(McReply(mc_res_unknown));
        return;
      }
      dispatchRequest(std::move(preq));
    }
    break;

    case ProxyMessage::Type::OLD_CONFIG:
    {
      auto oldConfig = reinterpret_cast<old_config_req_t*>(data);
      delete oldConfig;
    }
    break;

    case ProxyMessage::Type::DISCONNECT:
    {
      auto client = reinterpret_cast<McrouterClient*>(data);
      client->performDisconnect();
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

void proxy_t::routeHandlesProcessRequest(
  std::unique_ptr<ProxyRequestContext> upreq) {

  if (upreq->origReq()->op == mc_op_stats) {
    upreq->sendReply(
      stats_reply(this, to<folly::StringPiece>(upreq->origReq()->key)));
    return;
  }

  auto preq = ProxyRequestContext::process(std::move(upreq), getConfig());
  if (preq->origReq()->op == mc_op_get_service_info) {
    auto orig = preq->origReq().clone();
    const auto& config = preq->proxyConfig();
    McRequest req(std::move(orig));

    /* Will answer request for us */
    config.serviceInfo()->handleRequest(req, preq);
    return;
  }

  auto func_ctx = preq;

  fiberManager.addTaskFinally(
    [ctx = std::move(func_ctx)]() mutable {
      auto& origReq = ctx->origReq();
      try {
        auto& proute = ctx->proxyRoute();
        fiber_local::setSharedCtx(std::move(ctx));
        return proute.dispatchMcMsg(origReq.clone());
      } catch (const std::exception& e) {
        std::string err = "error routing "
          + to<std::string>(origReq->key) + ": " +
          e.what();
        return McReply(mc_res_local_error, err);
      }
    },
    [ctx = std::move(preq)](folly::Try<McReply>&& reply) {
      ctx->sendReply(std::move(*reply));
    }
  );
}

void proxy_t::processRequest(std::unique_ptr<ProxyRequestContext> preq) {
  assert(!preq->processing_);
  preq->processing_ = true;
  ++numRequestsProcessing_;
  stat_incr(stats, proxy_reqs_processing_stat, 1);

  switch (preq->origReq()->op) {
    case mc_op_stats:
      stat_incr(stats, cmd_stats_stat, 1);
      stat_incr(stats, cmd_stats_count_stat, 1);
      break;
    case mc_op_cas:
      stat_incr(stats, cmd_cas_stat, 1);
      stat_incr(stats, cmd_cas_count_stat, 1);
      break;
    case mc_op_get:
      stat_incr(stats, cmd_get_stat, 1);
      stat_incr(stats, cmd_get_count_stat, 1);
      break;
    case mc_op_gets:
      stat_incr(stats, cmd_gets_stat, 1);
      stat_incr(stats, cmd_gets_count_stat, 1);
      break;
    case mc_op_metaget:
      stat_incr(stats, cmd_meta_stat, 1);
      break;
    case mc_op_add:
      stat_incr(stats, cmd_add_stat, 1);
      stat_incr(stats, cmd_add_count_stat, 1);
      break;
    case mc_op_replace:
      stat_incr(stats, cmd_replace_stat, 1);
      stat_incr(stats, cmd_replace_count_stat, 1);
      break;
    case mc_op_set:
      stat_incr(stats, cmd_set_stat, 1);
      stat_incr(stats, cmd_set_count_stat, 1);
      break;
    case mc_op_incr:
      stat_incr(stats, cmd_incr_stat, 1);
      stat_incr(stats, cmd_incr_count_stat, 1);
      break;
    case mc_op_decr:
      stat_incr(stats, cmd_decr_stat, 1);
      stat_incr(stats, cmd_decr_count_stat, 1);
      break;
    case mc_op_delete:
      stat_incr(stats, cmd_delete_stat, 1);
      stat_incr(stats, cmd_delete_count_stat, 1);
      break;
    case mc_op_lease_set:
      stat_incr(stats, cmd_lease_set_stat, 1);
      stat_incr(stats, cmd_lease_set_count_stat, 1);
      break;
    case mc_op_lease_get:
      stat_incr(stats, cmd_lease_get_stat, 1);
      stat_incr(stats, cmd_lease_get_count_stat, 1);
      break;
    default:
      stat_incr(stats, cmd_other_stat, 1);
      stat_incr(stats, cmd_other_count_stat, 1);
      break;
  }

  routeHandlesProcessRequest(std::move(preq));

  stat_incr(stats, request_sent_stat, 1);
  stat_incr(stats, request_sent_count_stat, 1);
}

void proxy_t::dispatchRequest(std::unique_ptr<ProxyRequestContext> preq) {
  if (rateLimited(*preq)) {
    if (opts.proxy_max_throttled_requests > 0 &&
        numRequestsWaiting_ >= opts.proxy_max_throttled_requests) {
      preq->sendReply(McReply(mc_res_local_error, "Max throttled exceeded"));
      return;
    }
    auto& queue = waitingRequests_[static_cast<int>(preq->priority())];
    auto w = folly::make_unique<WaitingRequest>(std::move(preq));
    queue.pushBack(std::move(w));
    ++numRequestsWaiting_;
    stat_incr(stats, proxy_reqs_waiting_stat, 1);
  } else {
    processRequest(std::move(preq));
  }
}

bool proxy_t::rateLimited(const ProxyRequestContext& preq) const {
  if (!opts.proxy_max_inflight_requests) {
    return false;
  }

  /* Always let through certain requests */
  if (preq.origReq()->op == mc_op_stats ||
      preq.origReq()->op == mc_op_version ||
      preq.origReq()->op == mc_op_get_service_info) {
    return false;
  }

  if (waitingRequests_[static_cast<int>(preq.priority())].empty() &&
      numRequestsProcessing_ < opts.proxy_max_inflight_requests) {
    return false;
  }

  return true;
}

proxy_t::WaitingRequest::WaitingRequest(std::unique_ptr<ProxyRequestContext> r)
    : request(std::move(r)) {}

void proxy_t::pump() {
  auto numPriorities = static_cast<int>(ProxyRequestPriority::kNumPriorities);
  for (int i = 0; i < numPriorities; ++i) {
    auto& queue = waitingRequests_[i];
    while (numRequestsProcessing_ < opts.proxy_max_inflight_requests &&
           !queue.empty()) {
      --numRequestsWaiting_;
      auto w = queue.popFront();
      stat_decr(stats, proxy_reqs_waiting_stat, 1);

      processRequest(std::move(w->request));
    }
  }
}

uint64_t proxy_t::nextRequestId() {
  return ++nextReqId_;
}

/** allocate a new reply with piggybacking copy of str and the appropriate
    fields of the value nstring pointing to it.
    str may be nullptr for no piggybacking string.

    @return nullptr on failure
*/
MutableMcMsgRef new_reply(const char* str) {
  if (str == nullptr) {
    return createMcMsgRef();
  }
  size_t n = strlen(str);

  auto reply = createMcMsgRef(n + 1);
  reply->value.str = (char*) &(reply.get()[1]);

  memcpy(reply->value.str, str, n);
  reply->value.len = n;
  reply->value.str[n] = '\0';

  return reply;
}

std::shared_ptr<ShadowSettings>
ShadowSettings::create(const folly::dynamic& json, McrouterInstance* router) {
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
  } catch (const std::logic_error& e) {
    logFailure(router, failure::Category::kInvalidConfig,
               "ShadowSettings: {}", e.what());
    return nullptr;
  }

  if (router) {
    result->registerOnUpdateCallback(router);
  }

  return result;
}

void ShadowSettings::setKeyRange(double start, double end) {
  checkLogic(0 <= start && start <= end && end <= 1,
             "invalid key_fraction_range [{}, {}]", start, end);
  uint64_t keyStart = start * std::numeric_limits<uint32_t>::max();
  uint64_t keyEnd = end * std::numeric_limits<uint32_t>::max();
  keyRange_ = (keyStart << 32UL) | keyEnd;
}

ShadowSettings::~ShadowSettings() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

void ShadowSettings::registerOnUpdateCallback(McrouterInstance* router) {
  handle_ = router->rtVarsData().subscribeAndCall(
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
