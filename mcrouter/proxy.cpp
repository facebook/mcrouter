/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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

#include "mcrouter/_router.h"
#include "mcrouter/async.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/fibers/EventBaseLoopController.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
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

inline void shift_nstring_inplace(nstring_t* nstr, int pos) {
  nstr->str += pos;
  nstr->len -= pos;
}

void foreachPossibleClientHelper(const McrouterRouteHandleIf& rh,
                                 const RecordingMcRequest& req,
                                 const std::string& key) {
  auto children = rh.couldRouteTo(req, McOperation<mc_op_get>());
  for (const auto& it : children) {
    foreachPossibleClientHelper(*it, req, key);
  }
}

}  // anonymous namespace

const std::string kInternalGetPrefix = "__mcrouter__.";

static asox_queue_callbacks_t const proxy_request_queue_cb =  {
  /* Note that we want to drain the queue on cleanup,
     so we register both regular and sweep callbacks */
  mcrouter_request_ready_cb,
  mcrouter_request_ready_cb,
};

namespace {

FiberManager::Options getFiberManagerOptions(const McrouterOptions& opts) {
  FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.debugRecordStackUsed = opts.fibers_debug_record_stack_size;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  return fmOpts;
}

}

proxy_t::proxy_t(mcrouter_t *router_,
                 folly::EventBase* eventBase_,
                 const McrouterOptions& opts_)
    : router(router_),
      opts(opts_),
      eventBase(eventBase_),
      destinationMap(folly::make_unique<ProxyDestinationMap>(this)),
      durationUs(kExponentialFactor),
      randomGenerator(folly::randomNumberSeed()),
      fiberManager(folly::make_unique<EventBaseLoopController>(),
                   getFiberManagerOptions(opts_)) {
  TAILQ_INIT(&waitingRequests_);

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
  FBI_ASSERT(eventBase == nullptr);
  FBI_ASSERT(eventBase_ != nullptr);
  eventBase = eventBase_;
  onEventBaseAttached();
}

void proxy_t::onEventBaseAttached() {
  dynamic_cast<EventBaseLoopController&>(
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
    router->startupLock.notify();
  }
}

std::shared_ptr<ProxyConfigIf> proxy_t::getConfig() const {
  std::lock_guard<SFRReadLock> lg(
    const_cast<SFRLock&>(configLock_).readLock());
  return config_;
}

std::shared_ptr<ProxyConfigIf> proxy_t::swapConfig(
  std::shared_ptr<ProxyConfigIf> newConfig) {

  std::lock_guard<SFRWriteLock> lg(configLock_.writeLock());
  auto old = std::move(config_);
  config_ = std::move(newConfig);
  return old;
}

void proxy_t::foreachPossibleClient(
    const std::string& key,
    std::function<void(const ProxyClientCommon&)> callback) const {

  auto ctx = std::make_shared<RecordingContext>(std::move(callback));
  RecordingMcRequest req(ctx, key);

  auto config = getConfig();
  auto children =
    config->proxyRoute().couldRouteTo(req, McOperation<mc_op_get>());
  for (const auto& it : children) {
    foreachPossibleClientHelper(*it, req, key);
  }
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

proxy_request_t::proxy_request_t(proxy_t* p,
                                 McMsgRef req,
                                 void (*enqReply)(proxy_request_t* preq),
                                 void *con,
                                 void (*reqComplete)(proxy_request_t* preq),
                                 uint64_t senderId)
  : proxy(p),
    reply(mc_res_unknown),
    reply_state(REPLY_STATE_NO_REPLY),
    delay_reply(0),
    failover_disabled(0),
    _refcount(1),
    sender_id(senderId),
    requester(nullptr),
    legacy_get_service_info(false),
    context(con),
    enqueueReply_(enqReply),
    reqComplete_(reqComplete),
    processing_(false) {

  auto reqError = mc_client_req_check(req.get());
  if (reqError != mc_req_err_valid) {
    throw BadKeyException(mc_req_err_to_string(reqError));
  }

  if (req->op == mc_op_get && !strncmp(req->key.str, kInternalGetPrefix.c_str(),
                                       kInternalGetPrefix.size())) {
    /* HACK: for backwards compatibility, convert (get, "__mcrouter__.key")
       into (get-service-info, "key") */
    legacy_get_service_info = true;
    auto copy = MutableMcMsgRef(mc_msg_dup(req.get()));
    copy->op = mc_op_get_service_info;
    shift_nstring_inplace(&copy->key, kInternalGetPrefix.size());
    orig_req = std::move(copy);
  } else {
    orig_req = std::move(req);
  }

  stat_incr_safe(proxy->stats, proxy_request_num_outstanding_stat);
}

proxy_request_t::~proxy_request_t() {
  if (processing_) {
    assert(proxy);
    --proxy->numRequestsProcessing_;
    stat_decr(proxy->stats, proxy_reqs_processing_stat, 1);
    proxy->pump();
  }

  if (requester) {
    mcrouter_client_decref(requester);
  }
}

void proxy_request_decref(proxy_request_t* preq) {
  FBI_ASSERT(preq && preq->_refcount > 0);

  proxy_t* proxy = preq->proxy;

  if (preq->_refcount == 1) {
    if (proxy->opts.sync &&
        preq->reply_state == REPLY_STATE_REPLIED) {
      preq->enqueueReply_(preq);
    }
    if (preq->reqComplete_) {
      preq->reqComplete_(preq);
    }
  }

  if (--preq->_refcount > 0) {
    return;
  }

  delete preq;

  stat_decr_safe(proxy->stats, proxy_request_num_outstanding_stat);
}

proxy_request_t* proxy_request_incref(proxy_request_t* preq) {
  FBI_ASSERT(preq->_refcount > 0);
  preq->_refcount++;
  return preq;
}

/**
 * Deleter that deletes the object on the main context
 */
struct MainContextDeleter {
  void operator() (GenericProxyRequestContext* ctx) const {
    fiber::runInMainContext(
      [ctx] () {
        delete ctx;
      }
    );
  }
};

void proxy_t::routeHandlesProcessRequest(proxy_request_t* preq) {
  FBI_ASSERT(preq);
  FBI_ASSERT(preq->proxy);

  if (preq->orig_req->op == mc_op_stats) {
    preq->sendReply(
      stats_reply(this, to<folly::StringPiece>(preq->orig_req->key)));
    return;
  }

  auto config = getConfig();
  /* Note: we want MainContextDeleter here since the destructor
     can do complicated things, like finalize stats entry and
     destroy a stale config.  We assume that there's not enough
     stack space for these operations. */
  std::shared_ptr<GenericProxyRequestContext> ctx(
    new GenericProxyRequestContext(preq, config), MainContextDeleter());

  if (preq->orig_req->op == mc_op_get_service_info) {
    auto orig = ctx->ctx().proxyRequest().orig_req.clone();
    ProxyMcRequest req(std::move(ctx), std::move(orig));

    /* Will answer request for us */
    config->serviceInfo()->handleRequest(req);
    return;
  }

  auto func_ctx = folly::makeMoveWrapper(
    std::shared_ptr<GenericProxyRequestContext>(ctx));
  auto finally_ctx = folly::makeMoveWrapper(std::move(ctx));

  fiberManager.addTaskFinally(
    [func_ctx]() {
      auto& origReq = (*func_ctx)->ctx().proxyRequest().orig_req;
      try {
        auto& proute = (*func_ctx)->ctx().proxyRoute();
        auto reply = proute.dispatchMcMsg(origReq.clone(),
                                          std::move(*func_ctx));
        return ProxyMcReply::moveToMcReply(std::move(reply));
      } catch (const std::exception& e) {
        std::string err = "error routing "
          + to<std::string>(origReq->key) + ": " +
          e.what();
        return McReply(mc_res_local_error, err);
      }
    },
    [finally_ctx](folly::wangle::Try<McReply>&& reply) {
      (*finally_ctx)->ctx().proxyRequest().sendReply(std::move(*reply));
    }
  );
}

void proxy_t::processRequest(proxy_request_t* preq) {
  assert(!preq->processing_);
  preq->processing_ = true;
  ++numRequestsProcessing_;
  stat_incr(stats, proxy_reqs_processing_stat, 1);

  switch (preq->orig_req->op) {
    case mc_op_stats:
      stat_incr(stats, cmd_stats_stat, 1);
      stat_incr(stats, cmd_stats_count_stat, 1);
      break;
    case mc_op_get:
      stat_incr(stats, cmd_get_stat, 1);
      stat_incr(stats, cmd_get_count_stat, 1);
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

  routeHandlesProcessRequest(preq);

  stat_incr(stats, request_sent_stat, 1);
  stat_incr(stats, request_sent_count_stat, 1);
}

void proxy_t::dispatchRequest(proxy_request_t* preq) {
  if (rateLimited(preq)) {
    if (opts.proxy_max_throttled_requests > 0 &&
        numRequestsWaiting_ >= opts.proxy_max_throttled_requests) {
      preq->sendReply(McReply(mc_res_local_error, "Max throttled exceeded"));
      return;
    }
    proxy_request_incref(preq);
    // TODO(bwatling): replace waitingRequests_ with folly::CountedIntrusiveList
    TAILQ_INSERT_TAIL(&waitingRequests_, preq, entry_);
    numRequestsWaiting_ += 1;
    stat_incr(stats, proxy_reqs_waiting_stat, 1);
  } else {
    processRequest(preq);
  }
}

bool proxy_t::rateLimited(const proxy_request_t* preq) const {
  if (!opts.proxy_max_inflight_requests) {
    return false;
  }

  /* Always let through certain requests */
  if (preq->orig_req->op == mc_op_stats ||
      preq->orig_req->op == mc_op_version ||
      preq->orig_req->op == mc_op_get_service_info) {
    return false;
  }

  if (TAILQ_EMPTY(&waitingRequests_) &&
      numRequestsProcessing_ < opts.proxy_max_inflight_requests) {
    return false;
  }

  return true;
}

void proxy_t::pump() {
  while (numRequestsProcessing_ < opts.proxy_max_inflight_requests &&
         !TAILQ_EMPTY(&waitingRequests_)) {
    auto preq = TAILQ_FIRST(&waitingRequests_);
    TAILQ_REMOVE(&waitingRequests_, preq, entry_);
    numRequestsWaiting_ -= 1;
    stat_decr(stats, proxy_reqs_waiting_stat, 1);
    processRequest(preq);
    proxy_request_decref(preq);
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

void proxy_request_t::continueSendReply() {
  reply_state = REPLY_STATE_REPLIED;

  if (!proxy->opts.sync) {
    enqueueReply_(this);
  }

  stat_incr(proxy->stats, request_replied_stat, 1);
  stat_incr(proxy->stats, request_replied_count_stat, 1);
  if (mc_res_is_err(reply.result())) {
    stat_incr(proxy->stats, request_error_stat, 1);
    stat_incr(proxy->stats, request_error_count_stat, 1);
  } else {
    stat_incr(proxy->stats, request_success_stat, 1);
    stat_incr(proxy->stats, request_success_count_stat, 1);
  }
}

void proxy_request_t::sendReply(McReply newReply) {
  // Make sure we don't set the reply twice for a reply
  FBI_ASSERT(reply.result() == mc_res_unknown);
  FBI_ASSERT(newReply.result() != mc_res_unknown);

  reply = std::move(newReply);

  if (reply_state != REPLY_STATE_NO_REPLY) {
    return;
  }

  if (delay_reply == 0) {
    continueSendReply();
  } else {
    reply_state = REPLY_STATE_REPLY_DELAYED;
  }
}

void proxy_on_continue_reply_error(proxy_t* proxy, writelog_entry_t* e) {
  if (e->preq->reply_state == REPLY_STATE_REPLY_DELAYED &&
      e->preq->delay_reply == 1) {
    e->preq->continueSendReply();
  }

  writelog_entry_free(e);
}

proxy_pool_shadowing_policy_t::Data::Data()
    : start_index(0),
      end_index(0),
      start_key_fraction(0.0),
      end_key_fraction(0.0),
      shadow_pool(nullptr),
      shadow_type(DEFAULT_SHADOW_POLICY),
      validate_replies(false) {
}

proxy_pool_shadowing_policy_t::Data::Data(const folly::dynamic& json)
    : start_index(0),
      end_index(0),
      start_key_fraction(0.0),
      end_key_fraction(0.0),
      shadow_pool(nullptr),
      shadow_type(DEFAULT_SHADOW_POLICY),
      validate_replies(false) {
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

proxy_pool_shadowing_policy_t::proxy_pool_shadowing_policy_t(
  const folly::dynamic& json, mcrouter_t* router)
    : data_(std::make_shared<Data>(json)) {

  if (router) {
    registerOnUpdateCallback(router);
  }
}

proxy_pool_shadowing_policy_t::proxy_pool_shadowing_policy_t(
  std::shared_ptr<Data> data,
  mcrouter_t* router)
    : data_(std::move(data)) {

  if (router) {
    registerOnUpdateCallback(router);
  }
}

proxy_pool_shadowing_policy_t::~proxy_pool_shadowing_policy_t() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

std::shared_ptr<const proxy_pool_shadowing_policy_t::Data>
proxy_pool_shadowing_policy_t::getData() {
  return data_.get();
}

void proxy_pool_shadowing_policy_t::registerOnUpdateCallback(
    mcrouter_t* router) {
  handle_ = router->rtVarsData.subscribeAndCall(
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

static void proxy_config_swap(proxy_t* proxy,
                              std::shared_ptr<ProxyConfig> config) {
  /* Update the number of server stat for this proxy. */
  stat_set_uint64(proxy->stats, num_servers_stat, config->getClients().size());

  auto oldConfig = proxy->swapConfig(std::move(config));
  stat_set_uint64(proxy->stats, config_last_success_stat, time(nullptr));

  if (oldConfig) {
    if (!proxy->opts.sync) {
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
}

int router_configure(mcrouter_t* router, folly::StringPiece input) {
  FBI_ASSERT(router);

  size_t proxyCount = router->opts.num_proxies;
  std::vector<std::shared_ptr<ProxyConfig>> newConfigs;
  try {
    // assume default_route, default_region and default_cluster are same for
    // each proxy
    ProxyConfigBuilder builder(
      router->opts,
      router->configApi.get(),
      input);

    for (size_t i = 0; i < proxyCount; i++) {
      newConfigs.push_back(builder.buildConfig(router->getProxy(i)));
    }
  } catch (const std::exception& e) {
    logFailure(router, failure::Category::kInvalidConfig,
               "Failed to reconfigure: {}", e.what());
    return 0;
  }

  for (size_t i = 0; i < proxyCount; i++) {
    proxy_config_swap(router->getProxy(i), newConfigs[i]);
  }

  VLOG_IF(0, !router->opts.constantly_reload_configs) <<
      "reconfigured " << proxyCount << " proxies with " <<
      newConfigs[0]->getClients().size() << " clients (" <<
      newConfigs[0]->getConfigMd5Digest() << ")";

  return 1;
}

/** (re)configure the proxy. 1 on success, 0 on error.
    NB file-based configuration is synchronous
    but server-based configuration is asynchronous */
int router_configure(mcrouter_t *router) {
  int success = 0;

  {
    std::lock_guard<std::mutex> lg(router->config_reconfig_lock);
    /* mark config attempt before, so that
       successful config is always >= last config attempt. */
    router->last_config_attempt = time(nullptr);

    router->configApi->trackConfigSources();
    std::string config;
    success = router->configApi->getConfigFile(config);
    if (success) {
      success = router_configure_from_string(router, config);
    } else {
      logFailure(router, failure::Category::kBadEnvironment,
                 "Can not read config file");
    }

    if (!success) {
      router->config_failures++;
      router->configApi->abandonTrackedSources();
    } else {
      router->configApi->subscribeToTrackedSources();
    }
  }

  return success;
}

ProxyPool::ProxyPool(std::string name)
    : ProxyGenericPool(std::move(name)),
      hash(proxy_hash_crc32),
      protocol(mc_unknown_protocol),
      transport(mc_unknown_transport),
      delete_time(0),
      keep_routing_prefix(0),
      devnull_asynclog(false),
      failover_exptime(0),
      pool_failover_policy(nullptr) {

  memset(&timeout, 0, sizeof(timeval_t));
}

ProxyPool::~ProxyPool() {
  for (auto& c : clients) {
    auto client = c.lock();
    if (client) {
      /* Clearing the pool pointer in the pclient should only be done
         if the pclient's pool matches the one being freed.  The
         reconfiguration code reuses pclients, and may have
         readjusted the pclient's pool to point to a new pool. */
      if (client->pool == this) {
        client->pool = nullptr;
      }
    }
  }
  if (pool_failover_policy) {
    delete pool_failover_policy;
  }
}

ProxyMigratedPool::ProxyMigratedPool(std::string name)
    : ProxyGenericPool(std::move(name)),
      from_pool(nullptr),
      to_pool(nullptr),
      migration_start_ts(0),
      migration_interval_sec(0),
      warming_up(false),
      warmup_exptime(0) {
}

proxy_pool_failover_policy_t::proxy_pool_failover_policy_t() {
  memset(op, 0, sizeof(op));
}

}}} // facebook::memcache::mcrouter
