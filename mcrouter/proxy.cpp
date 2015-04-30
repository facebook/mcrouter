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
#include "mcrouter/ProxyMcRequest.h"
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

static asox_queue_callbacks_t const proxy_request_queue_cb =  {
  /* Note that we want to drain the queue on cleanup,
     so we register both regular and sweep callbacks */
  McrouterClient::requestReady,
  McrouterClient::requestReady,
};

folly::fibers::FiberManager::Options getFiberManagerOptions(
    const McrouterOptions& opts) {
  folly::fibers::FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.recordStackEvery = opts.fibers_record_stack_size_every;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  return fmOpts;
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

  int priority = get_event_priority(opts, SERVER_REQUEST);
  request_queue = asox_queue_init(eventBase->getLibeventBase(), priority,
                                  1, 0, 0, &proxy_request_queue_cb,
                                  ASOX_QUEUE_INTRA_PROCESS, this);

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
  if (request_queue) {
    asox_queue_del(request_queue);
  }

  magic = 0xdeadbeefdeadbeefLL;
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
    ProxyMcRequest req(std::move(orig));

    /* Will answer request for us */
    config.serviceInfo()->handleRequest(req, preq);
    return;
  }

  auto func_ctx = preq;

#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
  fiberManager.addTaskFinally(
    [ctx = std::move(func_ctx)]() mutable {
      auto& origReq = ctx->origReq();
      try {
        auto& proute = ctx->proxyRoute();
        fiber_local::setSharedCtx(std::move(ctx));
        auto reply = proute.dispatchMcMsg(origReq.clone());
        return ProxyMcReply::moveToMcReply(std::move(reply));
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
#ifdef __clang__
#pragma clang diagnostic pop
#endif
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
        waitingRequests_.size() >= opts.proxy_max_throttled_requests) {
      preq->sendReply(McReply(mc_res_local_error, "Max throttled exceeded"));
      return;
    }
    auto w = folly::make_unique<WaitingRequest>(std::move(preq));
    waitingRequests_.pushBack(std::move(w));
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

  if (waitingRequests_.empty() &&
      numRequestsProcessing_ < opts.proxy_max_inflight_requests) {
    return false;
  }

  return true;
}

proxy_t::WaitingRequest::WaitingRequest(std::unique_ptr<ProxyRequestContext> r)
    : request(std::move(r)) {}

void proxy_t::pump() {
  while (numRequestsProcessing_ < opts.proxy_max_inflight_requests &&
         !waitingRequests_.empty()) {
    auto w = waitingRequests_.popFront();
    stat_decr(stats, proxy_reqs_waiting_stat, 1);

    processRequest(std::move(w->request));
  }
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

ShadowSettings::Data::Data(const folly::dynamic& json) {
  checkLogic(json.isObject(), "shadowing_policy is not object");
  if (json.count("index_range")) {
    checkLogic(json["index_range"].isArray(),
               "shadowing_policy: index_range is not array");
    auto ar = folly::convertTo<std::vector<size_t>>(json["index_range"]);
    checkLogic(ar.size() == 2, "shadowing_policy: index_range size is not 2");
    start_index = ar[0];
    end_index = ar[1];
    checkLogic(start_index <= end_index,
               "shadowing_policy: index_range start > end");
  }
  if (json.count("key_fraction_range")) {
    checkLogic(json["key_fraction_range"].isArray(),
               "shadowing_policy: key_fraction_range is not array");
    auto ar = folly::convertTo<std::vector<double>>(json["key_fraction_range"]);
    checkLogic(ar.size() == 2,
               "shadowing_policy: key_fraction_range size is not 2");
    start_key_fraction = ar[0];
    end_key_fraction = ar[1];
    checkLogic(0 <= start_key_fraction &&
               start_key_fraction <= end_key_fraction &&
               end_key_fraction <= 1,
               "shadowing_policy: invalid key_fraction_range");
  }
  if (json.count("index_range_rv")) {
    checkLogic(json["index_range_rv"].isString(),
               "shadowing_policy: index_range_rv is not string");
    index_range_rv = json["index_range_rv"].asString().toStdString();
  }
  if (json.count("key_fraction_range_rv")) {
    checkLogic(json["key_fraction_range_rv"].isString(),
               "shadowing_policy: key_fraction_range_rv is not string");
    key_fraction_range_rv =
      json["key_fraction_range_rv"].asString().toStdString();
  }
}

ShadowSettings::ShadowSettings(const folly::dynamic& json,
                               McrouterInstance* router)
    : data_(std::make_shared<Data>(json)) {

  if (router) {
    registerOnUpdateCallback(router);
  }
}

ShadowSettings::ShadowSettings(std::shared_ptr<Data> data,
                               McrouterInstance* router)
    : data_(std::move(data)) {

  if (router) {
    registerOnUpdateCallback(router);
  }
}

ShadowSettings::~ShadowSettings() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

std::shared_ptr<const ShadowSettings::Data> ShadowSettings::getData() {
  return data_.get();
}

void ShadowSettings::registerOnUpdateCallback(McrouterInstance* router) {
  handle_ = router->rtVarsData().subscribeAndCall(
    [this](std::shared_ptr<const RuntimeVarsData> oldVars,
           std::shared_ptr<const RuntimeVarsData> newVars) {
      if (!newVars) {
        return;
      }
      auto dataCopy = std::make_shared<Data>(*this->getData());
      size_t start_index_temp = 0, end_index_temp = 0;
      double start_key_fraction_temp = 0, end_key_fraction_temp = 0;
      bool updateRange = false, updateKeyFraction = false;
      if (!dataCopy->index_range_rv.empty()) {
        auto valIndex =
            newVars->getVariableByName(dataCopy->index_range_rv);
        if (valIndex != nullptr) {
          checkLogic(valIndex.isArray(), "index_range_rv is not an array");

          checkLogic(valIndex.size() == 2, "Size of index_range_rv is not 2");

          checkLogic(valIndex[0].isInt(), "start_index is not an int");
          checkLogic(valIndex[1].isInt(), "end_index is not an int");
          start_index_temp = valIndex[0].asInt();
          end_index_temp = valIndex[1].asInt();
          checkLogic(start_index_temp <= end_index_temp,
                     "start_index > end_index");
          updateRange = true;
        }
      }
      if (!dataCopy->key_fraction_range_rv.empty()) {
        auto valFraction =
            newVars->getVariableByName(dataCopy->key_fraction_range_rv);
        if (valFraction != nullptr) {
          checkLogic(valFraction.isArray(),
                     "key_fraction_range_rv is not an array");
          checkLogic(valFraction.size() == 2,
                     "Size of key_fraction_range_rv is not 2");
          checkLogic(valFraction[0].isNumber(),
                     "start_key_fraction is not a number");
          checkLogic(valFraction[1].isNumber(),
                     "end_key_fraction is not a number");
          start_key_fraction_temp = valFraction[0].asDouble();
          end_key_fraction_temp = valFraction[1].asDouble();

          checkLogic(start_key_fraction_temp >= 0.0 &&
                     start_key_fraction_temp <= 1.0 &&
                     end_key_fraction_temp >= 0.0 &&
                     end_key_fraction_temp <= 1.0 &&
                     start_key_fraction_temp <= end_key_fraction_temp,
                     "Invalid values for start_key_fraction and/or "
                     "end_key_fraction");

          updateKeyFraction = true;
        }
      }

      if (updateRange) {
        dataCopy->start_index = start_index_temp;
        dataCopy->end_index = end_index_temp;
      }
      if (updateKeyFraction) {
        dataCopy->start_key_fraction = start_key_fraction_temp;
        dataCopy->end_key_fraction = end_key_fraction_temp;
      }

      this->data_.set(std::move(dataCopy));
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
    asox_queue_entry_t oldConfigEntry;
    oldConfigEntry.data = configReq;
    oldConfigEntry.nbytes = sizeof(*configReq);
    oldConfigEntry.priority = 0;
    oldConfigEntry.type = request_type_old_config;
    oldConfigEntry.time_enqueued = time(nullptr);
    asox_queue_enqueue(proxy->request_queue, &oldConfigEntry);
  }
}

}}} // facebook::memcache::mcrouter
