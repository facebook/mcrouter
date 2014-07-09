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

#include "folly/ThreadName.h"
#include "folly/FileUtil.h"
#include "folly/Format.h"
#include "folly/Memory.h"
#include "folly/Random.h"
#include "folly/Range.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/EventBaseLoopController.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyLogger.h"
#include "mcrouter/ProxyRequest.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/_router.h"
#include "mcrouter/async.h"
#include "mcrouter/config.h"
#include "mcrouter/options.h"
#include "mcrouter/priorities.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/stats.h"

using folly::wangle::Try;

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

inline void shift_nstring_inplace(nstring_t* nstr, int pos) {
  nstr->str += pos;
  nstr->len -= pos;
}

}  // anonymous namespace

const std::string kInternalGetPrefix = "__mcrouter__.";
const double kExponentialFactor = 1.0 / 64.0;

static void proxy_set_default_route(proxy_t* proxy, const std::string& str) {
  if (str.empty()) {
    return;
  }
  boost::regex routeRegex("^/[^/]+/[^/]+/?$");
  if (!boost::regex_match(str, routeRegex)) {
    LOG(ERROR) << "default route (" << str <<
                  ") should be of the form /region/cluster/";
    // Not setting the default route here causes proxy_validate_config() to
    // fail, so mcrouter will not start. It will have printed useful error
    // messages, so this seems like the right behavior.
    return;
  }

  proxy->default_route = str;
  if (str.back() != '/') {
    proxy->default_route += "/";
  }

  auto regionEnd = proxy->default_route.find('/', 1);
  FBI_ASSERT(regionEnd != std::string::npos);
  proxy->default_region = proxy->default_route.substr(1, regionEnd - 1);

  auto clusterEnd = proxy->default_route.find('/', regionEnd + 1);
  FBI_ASSERT(clusterEnd != std::string::npos);
  proxy->default_cluster =
    proxy->default_route.substr(regionEnd + 1, clusterEnd - regionEnd - 1);
}

static void proxy_init_timers(proxy_t *proxy) {
  if (!proxy->opts.disable_dynamic_stats) {
    nstring_t name_up = NSTRING_LIT("mcrouter_request_up");
    proxy->request_timer_up = fb_timer_alloc(name_up, 0, 0);
    nstring_t name_down = NSTRING_LIT("mcrouter_request_down");
    proxy->request_timer_down = fb_timer_alloc(name_down, 0, 0);
  }
}

static void proxy_del_timers(const proxy_t *proxy) {
  if (!proxy->opts.disable_dynamic_stats) {
    fb_timer_free(proxy->request_timer_up);
    fb_timer_free(proxy->request_timer_down);
  }
}

static asox_queue_callbacks_t const proxy_request_queue_cb =  {
  /* Note that we want to drain the queue on cleanup,
     so we register both regular and sweep callbacks */
  mcrouter_request_ready_cb,
  mcrouter_request_ready_cb,
};

ExponentialSmoothData::ExponentialSmoothData(double smoothingFactor)
  : smoothingFactor_(smoothingFactor) {
  assert(smoothingFactor_ >= 0.0 && smoothingFactor_ <= 1.0);
}

void ExponentialSmoothData::insertSample(double value) {
  if (LIKELY(hasRegisteredFirstSample_)) {
    currentValue_ = smoothingFactor_ * value +
                    (1 - smoothingFactor_) * currentValue_;
  } else {
    currentValue_ = value;
    hasRegisteredFirstSample_ = true;
  }
}

double ExponentialSmoothData::getCurrentValue() const {
  return currentValue_;
}

facebook::memcache::FiberManager::Options
proxy_t::getFiberManagerOptions(
  const facebook::memcache::McrouterOptions& opts) {

  FiberManager::Options fmOpts;
#ifdef __SANITIZE_ADDRESS__
  /* ASAN needs a lot of extra stack space.
     16x is a conservative estimate, 8x also worked with tests
     where it mattered.  Note that overallocating here does not necessarily
     increase RSS, since unused memory is pretty much free. */
  fmOpts.stackSize = opts.fibers_stack_size * 16;
#else
  fmOpts.stackSize = opts.fibers_stack_size;
#endif
  fmOpts.debugRecordStackUsed = opts.fibers_debug_record_stack_size;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  return fmOpts;
}

proxy_t::proxy_t(mcrouter_t *router_,
                 folly::EventBase* eventBase_,
                 const McrouterOptions& opts_,
                 bool perform_stats_logging)
    : router(router_),
      opts(opts_),
      request_queue(0),
      eventBase(eventBase_),
      destinationMap(folly::make_unique<ProxyDestinationMap>(this)),
      config_lock(std::make_shared<sfrlock_t>()),
      proxyThreadConfigReadLock(config_lock),
      async_fd(nullptr),
      async_spool_time(0),
      rtt_timer(0),
      request_timer_up(0),
      request_timer_down(0),
      durationUs(kExponentialFactor),
      num_bins_used(0),
      monitor(0),
      awriter(nullptr),
      awriter_thread_handle(0),
      awriter_thread_stack(0),
      id(-1),
      version(0),
      stats_log_writer_thread_handle(0),
      stats_log_writer(nullptr),
      stats_log_writer_thread_stack(0),
      randomGenerator(folly::randomNumberSeed()),
      being_destroyed(false),
      fiberManager(folly::make_unique<EventBaseLoopController>(),
                   getFiberManagerOptions(opts_)),
      numRequestsProcessing_(0),
      performStatsLogging_(perform_stats_logging) {
  TAILQ_INIT(&waitingRequests_);

  memset(stats, 0, sizeof(stats));
  memset(stats_bin, 0, sizeof(stats_bin));
  memset(stats_num_within_window, 0, sizeof(stats_num_within_window));

  static uint64_t next_magic = 0x12345678900000LL;

  magic = __sync_fetch_and_add(&next_magic, 1);

  proxy_set_default_route(this, opts.default_route);

  proxy_init_timers(this);
  init_stats(stats);

  sfrlock_init(config_lock.get());
  if (!opts.disable_dynamic_stats) {
    FBI_VERIFY(rtt_timer =
               fb_timer_alloc((nstring_t)NSTRING_LIT("proxy_rtt_timer"), 0,0));
  }

  /* TODO: Determine what the maximum queue length should be. */
  awriter = folly::make_unique<awriter_t>(0);
  stats_log_writer = folly::make_unique<awriter_t>(
      opts.stats_async_queue_length);

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

  if (performStatsLogging_ && router != nullptr
      && opts.stats_logging_interval != 0) {
    logger = createProxyLogger(this);
  }

  statsContainer = folly::make_unique<ProxyStatsContainer>(this);

  if (router != nullptr) {
    router->startupLock.notify();
  }
}

int proxy_start_awriter_threads(proxy_t* proxy, bool realtime) {
  if (!proxy->opts.asynclog_disable) {
    int rc = spawn_thread(&proxy->awriter_thread_handle,
                          &proxy->awriter_thread_stack, &awriter_thread_run,
                          proxy->awriter.get(), realtime);
    if (!rc) {
      LOG(ERROR) << "Failed to start asynclog awriter thread";
      return -1;
    }
    folly::setThreadName(proxy->awriter_thread_handle, "mcrtr-awriter");
  }

  int rc = spawn_thread(&proxy->stats_log_writer_thread_handle,
                        &proxy->stats_log_writer_thread_stack,
                        &awriter_thread_run,
                        proxy->stats_log_writer.get(), realtime);
  if (!rc) {
    LOG(ERROR) << "Failed to start async stats_log_writer thread";
    return -1;
  }

  folly::setThreadName(proxy->stats_log_writer_thread_handle, "mcrtr-statsw");

  return 0;
}

void proxy_stop_awriter_threads(proxy_t* proxy) {
  if (proxy->awriter_thread_handle) {
    awriter_stop(proxy->awriter.get());
    pthread_join(proxy->awriter_thread_handle, nullptr);
  }

  if (proxy->stats_log_writer_thread_handle) {
    awriter_stop(proxy->stats_log_writer.get());
    pthread_join(proxy->stats_log_writer_thread_handle, nullptr);
  }

  if (proxy->awriter_thread_stack) {
    free(proxy->awriter_thread_stack);
    proxy->awriter_thread_stack = nullptr;
  }

  if (proxy->stats_log_writer_thread_stack) {
    free(proxy->stats_log_writer_thread_stack);
    proxy->stats_log_writer_thread_stack = nullptr;
  }
}

/** drain and delete proxy object */
proxy_t::~proxy_t() {
  config.reset();
  destinationMap.reset();

  being_destroyed = true;
  if (request_queue) {
      asox_queue_del(request_queue);
  }

  proxy_del_timers(this);

  if (rtt_timer) {
    fb_timer_free(rtt_timer);
  }

  magic = 0xdeadbeefdeadbeefLL;
}

void proxy_set_monitor(proxy_t *proxy, proxy_client_monitor_t *mon) {
  if (mon) {
    FBI_ASSERT(mon->on_up);
    FBI_ASSERT(mon->on_response);
    FBI_ASSERT(mon->on_down);
    FBI_ASSERT(mon->may_send);
    FBI_ASSERT(mon->remove_client);
  }

  proxy->monitor = mon;
}

proxy_request_t::proxy_request_t(proxy_t* p,
                                 mc_msg_t* req,
                                 void (*enqReply)(proxy_request_t* preq),
                                 void *con,
                                 void (*reqComplete)(proxy_request_t* preq),
                                 uint64_t senderId)
  : proxy(p),
    reply(nullptr),
    reply_state(REPLY_STATE_NO_REPLY),
    delay_reply(0),
    failover_disabled(0),
    _refcount(1),
    sender_id(senderId),
    start_time_up(0),
    start_time_down(0),
    send_time(0),
    created_time(fb_timer_cycle_timer()),
    requester(nullptr),
    legacy_get_service_info(false),
    context(con),
    config_(p->config),
    enqueueReply_(enqReply),
    reqComplete_(reqComplete),
    processing_(false) {
  FBI_ASSERT(req != nullptr);

  if (!mc_client_req_is_valid(req)) {
    throw std::runtime_error("Invalid request");
  }

  if (req->op == mc_op_get && !strncmp(req->key.str, kInternalGetPrefix.c_str(),
                                       kInternalGetPrefix.size())) {
    /* HACK: for backwards compatibility, convert (get, "__mcrouter__.key")
       into (get-service-info, "key") */
    legacy_get_service_info = true;
    orig_req = mc_msg_dup(req);
    orig_req->op = mc_op_get_service_info;
    shift_nstring_inplace(&orig_req->key, kInternalGetPrefix.size());
  } else {
    orig_req = mc_msg_incref(req);
  }

  stat_incr_safe(proxy, proxy_request_num_outstanding_stat);
}

proxy_request_t::~proxy_request_t() {
  if (processing_) {
    assert(proxy);
    --proxy->numRequestsProcessing_;
    stat_decr(proxy, proxy_reqs_processing_stat, 1);
    proxy->pump();
  }

  if (reply) {
    mc_msg_decref(reply);
  }

  mc_msg_decref(orig_req);

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

  stat_decr_safe(proxy, proxy_request_num_outstanding_stat);
}

proxy_request_t* proxy_request_incref(proxy_request_t* preq) {
  FBI_ASSERT(preq->_refcount > 0);
  preq->_refcount++;
  return preq;
}

/**
 * extracts "region" part from "/region/cluster/" routing prefix
 * @return region if prefix is valid, empty StringPiece otherwise
 */
folly::StringPiece getRegionFromRoutingPrefix(folly::StringPiece prefix) {
  if (prefix.empty() || prefix[0] != '/') {
    return folly::StringPiece();
  }

  auto regEnd = prefix.find("/", 1);
  if (regEnd == std::string::npos) {
    return folly::StringPiece();
  }
  assert(regEnd >= 1);
  return prefix.subpiece(1, regEnd - 1);
}

void proxy_t::routeHandlesProcessRequest(proxy_request_t* preq) {
  FBI_ASSERT(preq);
  FBI_ASSERT(preq->proxy);

  if (preq->orig_req->op == mc_op_stats) {
    auto msg = McMsgRef::moveRef(
      stats_reply(this, to<folly::StringPiece>(preq->orig_req->key)));
    preq->sendReply(const_cast<mc_msg_t*>(msg.get()));
    return;
  }

  auto ctx = std::make_shared<ProxyRequestContext>(preq, config->proxyRoute());
  auto orig = McMsgRef::cloneRef(ctx->proxyRequest().orig_req);

  if (preq->orig_req->op == mc_op_get_service_info) {
    ProxyMcRequest req(ctx, orig.clone());

    /* Will answer request for us */
    config->serviceInfo()->handleRequest(req);
    return;
  }

  fiberManager.addTaskFinally(
    [ctx]() {
      auto origReq = McMsgRef::cloneRef(ctx->proxyRequest().orig_req);
      try {
        auto reply = ctx->proxyRoute().dispatchMcMsg(origReq, ctx);
        return reply.releasedMsg(origReq->op);
      } catch (const std::exception& e) {
        std::string err = "error routing "
          + to<std::string>(origReq->key) + ": " + e.what();
        return McMsgRef::moveRef(create_reply(origReq->op,
                                              mc_res_local_error,
                                              err.c_str()));
      }
    },
    [ctx](Try<McMsgRef>&& msg) {
      ctx->proxyRequest().sendReply(const_cast<mc_msg_t*>(msg->get()));
    }
  );
}

void proxy_t::processRequest(proxy_request_t* preq) {
  // make sure that we grab the config_lock before we route
  // to ensure the config doesn't change on us while routing
  std::lock_guard<ThreadReadLock> lock(proxyThreadConfigReadLock);

  assert(!preq->processing_);
  preq->processing_ = true;
  ++numRequestsProcessing_;
  stat_incr(this, proxy_reqs_processing_stat, 1);

  static fb_timer_t *on_request_timer = nullptr;

  if (!opts.disable_dynamic_stats) {
    if (!on_request_timer) {
      nstring_t name = NSTRING_LIT("router_on_request");
      on_request_timer = fb_timer_alloc(name, 0, 0);
      fb_timer_register(on_request_timer);
    }
    fb_timer_start(on_request_timer);
  }

  switch (preq->orig_req->op) {
    case mc_op_stats:
      stat_incr(this, cmd_stats_stat, 1);
      stat_incr(this, cmd_stats_count_stat, 1);
      break;
    case mc_op_get:
      stat_incr(this, cmd_get_stat, 1);
      stat_incr(this, cmd_get_count_stat, 1);
      break;
    case mc_op_metaget:
      stat_incr(this, cmd_meta_stat, 1);
      break;
    case mc_op_add:
      stat_incr(this, cmd_add_stat, 1);
      stat_incr(this, cmd_add_count_stat, 1);
      break;
    case mc_op_replace:
      stat_incr(this, cmd_replace_stat, 1);
      stat_incr(this, cmd_replace_count_stat, 1);
      break;
    case mc_op_set:
      stat_incr(this, cmd_set_stat, 1);
      stat_incr(this, cmd_set_count_stat, 1);
      break;
    case mc_op_incr:
      stat_incr(this, cmd_incr_stat, 1);
      stat_incr(this, cmd_incr_count_stat, 1);
      break;
    case mc_op_decr:
      stat_incr(this, cmd_decr_stat, 1);
      stat_incr(this, cmd_decr_count_stat, 1);
      break;
    case mc_op_delete:
      stat_incr(this, cmd_delete_stat, 1);
      stat_incr(this, cmd_delete_count_stat, 1);
      break;
    case mc_op_lease_set:
      stat_incr(this, cmd_lease_set_stat, 1);
      stat_incr(this, cmd_lease_set_count_stat, 1);
      break;
    case mc_op_lease_get:
      stat_incr(this, cmd_lease_get_stat, 1);
      stat_incr(this, cmd_lease_get_count_stat, 1);
      break;
    default:
      stat_incr(this, cmd_other_stat, 1);
      stat_incr(this, cmd_other_count_stat, 1);
      break;
  }

  routeHandlesProcessRequest(preq);

  stat_incr(this, request_sent_stat, 1);
  stat_incr(this, request_sent_count_stat, 1);
  if (!opts.disable_dynamic_stats) {
    fb_timer_finish(on_request_timer);
  }
}

void proxy_t::dispatchRequest(proxy_request_t* preq) {
  if (rateLimited(preq)) {
    proxy_request_incref(preq);
    TAILQ_INSERT_TAIL(&waitingRequests_, preq, entry_);
    stat_incr(this, proxy_reqs_waiting_stat, 1);
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
    stat_decr(this, proxy_reqs_waiting_stat, 1);
    processRequest(preq);
    proxy_request_decref(preq);
  }
}

/** allocate a new reply with piggybacking copy of str and the appropriate
    fields of the value nstring pointing to it.
    str may be nullptr for no piggybacking string.

    @return nullptr on failure
*/
static mc_msg_t* new_reply(const char* str) {
  if (str == nullptr) {
    return mc_msg_new(0);
  }
  size_t n = strlen(str);

  mc_msg_t* reply = mc_msg_new(n + 1);
  reply->value.str = (char*) &(reply[1]);

  if (reply == nullptr) {
    return nullptr;
  }

  memcpy(reply->value.str, str, n);
  reply->value.len = n;
  reply->value.str[n] = '\0';

  return reply;
}

mc_msg_t *create_reply(mc_op_t op, mc_res_t result, const char *str) {
  mc_msg_t *reply = new_reply(str);
  if (reply == nullptr) {
    LOG(ERROR) << "couldn't allocate reply: " << strerror(errno);
    return nullptr;
  }

  reply->op = op;
  reply->result = result;

  return reply;
}

void proxy_request_t::continueSendReply() {
  reply_state = REPLY_STATE_REPLIED;

  if (start_time_down && !proxy->opts.disable_dynamic_stats) {
    fb_timer_record_finish(proxy->request_timer_down,
                           start_time_down, fb_timer_cycle_timer());
  }

  if (!proxy->opts.sync) {
    enqueueReply_(this);
  }

  stat_incr(proxy, request_replied_stat, 1);
  stat_incr(proxy, request_replied_count_stat, 1);
  if (mc_res_is_err(reply->result)) {
    stat_incr(proxy, request_error_stat, 1);
    stat_incr(proxy, request_error_count_stat, 1);
  } else {
    stat_incr(proxy, request_success_stat, 1);
    stat_incr(proxy, request_success_count_stat, 1);
  }
}

void proxy_request_t::sendReply(mc_msg_t* newReply) {
  // Make sure we don't set the reply twice for a reply
  FBI_ASSERT(reply == nullptr);
  FBI_ASSERT(newReply);

  reply = mc_msg_incref(newReply);

  // undo op munging
  reply->op = legacy_get_service_info ? mc_op_get : orig_req->op;

  if (reply_state != REPLY_STATE_NO_REPLY) {
    return;
  }

  if (delay_reply == 0) {
    continueSendReply();
  } else {
    reply_state = REPLY_STATE_REPLY_DELAYED;
  }
}

bool proxy_request_t::enqueueReplyEquals(
    void (*funcPtr)(proxy_request_t* preq)) {
  return enqueueReply_ == funcPtr;
}

void proxy_on_continue_reply_error(proxy_t* proxy, writelog_entry_t* e) {
  if (e->preq->reply_state == REPLY_STATE_REPLY_DELAYED &&
      e->preq->delay_reply == 1) {
    e->preq->continueSendReply();
  }

  writelog_entry_free(e);
}

void proxy_flush_rtt_stats(proxy_t *proxy) {
  if (!proxy->opts.disable_dynamic_stats) {
    uint64_t rtt_min = fb_timer_get_avg_min(proxy->rtt_timer);
    stat_set_uint64(proxy, rtt_min_stat, rtt_min);
    uint64_t rtt = fb_timer_get_avg(proxy->rtt_timer);
    stat_set_uint64(proxy, rtt_stat, rtt);
    uint64_t rtt_max = fb_timer_get_avg_peak(proxy->rtt_timer);
    stat_set_uint64(proxy, rtt_max_stat, rtt_max);
  }
}

}}} // facebook::memcache::mcrouter
