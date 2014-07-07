/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyDestination.h"

#include <mcrouter/config-impl.h>

#include "folly/Conv.h"
#include "folly/Memory.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/TkoTracker.h"
#include "mcrouter/_router.h"
#include "mcrouter/dynamic_stats.h"
#include "mcrouter/pclient.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/DestinationRoute.h"
#include "mcrouter/stats.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/fbi/util.h"
#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/network/AsyncMcClient.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

constexpr double kProbeExponentialFactor = 1.5;
constexpr double kProbeJitterMin = 0.05;
constexpr double kProbeJitterMax = 0.5;
constexpr double kProbeJitterDelta = kProbeJitterMax - kProbeJitterMin;

static_assert(kProbeJitterMax >= kProbeJitterMin,
              "ProbeJitterMax should be greater or equal tham ProbeJitterMin");

void on_probe_timer(const asox_timer_t timer, void* arg) {
  ProxyDestination* pdstn = (ProxyDestination*)arg;
  pdstn->on_timer(timer);
}

bool is_error_reply(mc_res_t result, const mc_msg_t* reply) {
  if (result == mc_res_remote_error) {
    // mc_res_remote_error with a reply object is an application-level
    // error, not a network/server level error.
    return reply == nullptr;
  } else if (result == mc_res_try_again) {
    return false;
  } else {
    return mc_res_is_err(result);
  }
}

}  // anonymous namespace

void ProxyDestination::schedule_next_probe() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(!proxy->opts.disable_tko_tracking);

  int delay_ms = probe_delay_next_ms;
  if (probe_delay_next_ms < 2) {
    // int(1 * 1.5) == 1, so advance it to 2 first
    probe_delay_next_ms = 2;
  } else {
    probe_delay_next_ms *= kProbeExponentialFactor;
  }
  if (probe_delay_next_ms > proxy->opts.probe_delay_max_ms) {
    probe_delay_next_ms = proxy->opts.probe_delay_max_ms;
  }

  // Calculate random jitter
  double r = (double)rand() / (double)RAND_MAX;
  double tmo_jitter_pct = r * kProbeJitterDelta + kProbeJitterMin;
  uint64_t delay_us = (double)delay_ms * 1000 * (1.0 + tmo_jitter_pct);
  FBI_ASSERT(delay_us > 0);

  timeval_t delay;
  delay.tv_sec = (delay_us / 1000000);
  delay.tv_usec = (delay_us % 1000000);

  FBI_ASSERT(probe_timer == nullptr);
  probe_timer = asox_add_timer(proxy->eventBase->getLibeventBase(), delay,
                               on_probe_timer, this);
}

void ProxyDestination::on_timer(const asox_timer_t timer) {
  // Some unit tests create pdstn with null proxy.  This assert checks
  // for use-after-free, so allowing null proxy for functions that don't
  // obviously crash with null proxy doesn't reduce its effectiveness
  FBI_ASSERT(!proxy || proxy->magic == proxy_magic);
  FBI_ASSERT(timer == probe_timer);
  asox_remove_timer(timer);
  probe_timer = nullptr;
  if (sending_probes) {
    // Note that the previous probe might still be in flight
    if (probe_req == nullptr) {
      probe_req = mc_msg_new(0);
      FBI_ASSERT(probe_req);
      probe_req->op = mc_op_version;
      send(probe_req, nullptr, /* senderId= */ 0);
    }
    schedule_next_probe();
  }
}

void ProxyDestination::start_sending_probes() {
  FBI_ASSERT(!sending_probes);
  sending_probes = true;
  probe_delay_next_ms = proxy->opts.probe_delay_initial_ms;
  schedule_next_probe();
}

void ProxyDestination::stop_sending_probes() {
  sending_probes = false;
  if (probe_timer) {
    asox_remove_timer(probe_timer);
    probe_timer = nullptr;
  }
}

void ProxyDestination::mark_tko() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(!proxy->opts.disable_tko_tracking);
  if (!marked_tko) {
    VLOG(1) << pdstnKey << " marked TKO";

    marked_tko = 1;

    start_sending_probes();
  }
}

void ProxyDestination::unmark_tko() {
  FBI_ASSERT(!proxy->opts.disable_tko_tracking);
  if (marked_tko) {
    VLOG(1) << pdstnKey << " marked up";
    marked_tko = 0;
    stop_sending_probes();
  }
}

void ProxyDestination::unmark_global_tko() {
  FBI_ASSERT(!proxy->opts.disable_tko_tracking);
  FBI_ASSERT(proxy->router &&
             proxy->router->opts.global_tko_tracking);
  FBI_ASSERT(shared);
  shared->tko.recordSuccess();
  if (sending_probes) {
    VLOG(1) << pdstnKey << " marked up";
    stop_sending_probes();
  }
}

void ProxyDestination::track_latency(int64_t latency) {
  size_t window_size = proxy->opts.latency_window_size;
  // If window size is 0, the feature is gated so just return
  if (window_size == 0) {
    return;
  }
  /* We use a geometric moving average for speed and simplicity. Since latency
  is capped by timeout, this number can never explode, and samples will be
  weighted to effectively 0 over time */
  avgLatency_ = (latency + avgLatency_ * (window_size-1)) / window_size;
}

void ProxyDestination::handle_tko(mc_res_t result,
                                  const mc_msg_t* reply,
                                  int consecutive_errors) {
  if (resetting ||
      proxy->opts.disable_tko_tracking ||
      proxy->monitor != nullptr) {
    return;
  }

  if (proxy->router &&
      proxy->router->opts.global_tko_tracking) {

    if (!shared) {
      return;
    }

    bool responsible = false;
    if (is_error_reply(result, reply)) {
      if (result == mc_res_connect_error) {
        responsible = shared->tko.recordHardFailure();
      } else {
        responsible = shared->tko.recordSoftFailure();
      }
    } else if (proxy->opts.latency_window_size != 0 &&
        !sending_probes &&
        avgLatency_ > proxy->opts.latency_threshold_us) {
    /* Even if it's not an error, if we've gone above our latency SLA we count
       that as a soft failure. We also check current latency to ensure that
       if things get better we don't keep TKOing the box */
      responsible = shared->tko.recordSoftFailure();
    } else {
      unmark_global_tko();
    }
    if (responsible) {
      VLOG(1) << pdstnKey << " marked TKO";
      start_sending_probes();
    }
  } else {
    if (consecutive_errors >= proxy->opts.failures_until_tko) {
      mark_tko();
    } else if (consecutive_errors == 0) {
      unmark_tko();
    }
  }
}

void ProxyDestination::on_reply(mc_msg_t *req,
                                mc_msg_t *reply, const mc_res_t result,
                                void* req_ctx) {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(req);


  proxy_request_t* preq = nullptr;

  static fb_timer_t *on_reply_timer = nullptr;
  if (!proxy->opts.disable_dynamic_stats) {
    if (!on_reply_timer) {
      nstring_t name = NSTRING_LIT("router_on_reply");
      on_reply_timer = fb_timer_alloc(name, 0, 0);
      fb_timer_register(on_reply_timer);
    }
    fb_timer_start(on_reply_timer);
  }

  // Note: remote error with non-empty reply is not an actual error.
  // mc_res_busy with a code not SERVER_ERROR_BUSY means
  // the server is fine, just can't fulfil the request now
  if (mc_res_is_err(result) &&
      !(result == mc_res_remote_error && reply != nullptr) &&
      result != mc_res_try_again) {
    ++consecutiveErrors_;
  } else {
    consecutiveErrors_ = 0;
  }

  if (!is_error_reply(result, reply)) {
    /**
     * HACK: It's possible that we missed an on_up callback for this pdstn
     * if it happened right during a reconfigure request, but a successful
     * on_reply was still delivered.  So we just manually call on_up here.
     * Ideally we wouldn't miss on_up callbacks, but it's not easy to do.
     */
    if (!stats.is_up) {
      on_up();
    }
  }

  handle_tko(result, reply, consecutiveErrors_);

  if (req == probe_req) {
    probe_req = nullptr;
  } else {
    stats.results[result]++;

    auto destreqCtx = reinterpret_cast<DestinationRequestCtx*>(req_ctx);
    preq = destreqCtx->preq;

    if (proxy->monitor) {
      proxy->monitor->on_response(proxy->monitor, this,
                                  preq, req, reply, result);
    }

    destreqCtx->endTime = nowUs();
    destreqCtx->reply = reply;
    destreqCtx->result = result;

    /* For code simplicity we look at latency for making TKO decisions with a
       1 request delay */
    int64_t latency = destreqCtx->endTime - destreqCtx->startTime;
    track_latency(latency);

    auto promise = std::move(destreqCtx->promise.value());
    promise.setValue();
    reply = nullptr;
  }

  // The reply path gets a request in three states
  // (1) A request's refcount was incremented in send_to_mcc
  // (2) A request was duped from send_to_mcc
  // (3) A new probe request was issued
  // in all three cases we must decrement the associated refcount
  mc_msg_decref(req);

  if (reply != nullptr) {
    mc_msg_decref(reply);
  }
  if (preq) {
    stat_decr(proxy, sum_server_queue_length_stat, 1);
  }

  if (!proxy->opts.disable_dynamic_stats) {
    fb_timer_finish(on_reply_timer);
  }
}

void ProxyDestination::on_up() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(!stats.is_up);


  if (proxy->monitor) {
    proxy->monitor->on_up(proxy->monitor, this);
  }

  stat_incr(proxy, server_up_events_stat, 1);
  stat_incr(proxy, num_servers_up_stat, 1);

  stats.is_up = 1;
  gettimeofday(&up_time, 0);

  VLOG(1) << "server " << pdstnKey << " up (" <<
      stat_get_uint64(proxy, num_servers_up_stat) << " of " <<
      stat_get_uint64(proxy, num_servers_stat) << ")";
}

void ProxyDestination::on_down() {
  FBI_ASSERT(proxy->magic == proxy_magic);

  if (resetting) {
    VLOG(1) << "server " << pdstnKey << " inactive (" <<
        stat_get_uint64(proxy, num_servers_up_stat) << " of " <<
        stat_get_uint64(proxy, num_servers_stat) << ")";
    stat_incr(proxy, closed_inactive_connections_stat, 1);
    if (stats.is_up) {
      stat_decr(proxy, num_servers_up_stat, 1);
      stats.is_up = 0;
    }
  } else {
    VLOG(1) << "server " << pdstnKey << " down (" <<
        stat_get_uint64(proxy, num_servers_up_stat) << " of " <<
        stat_get_uint64(proxy, num_servers_stat) << ")";

    if (proxy->monitor) {
      proxy->monitor->on_down(proxy->monitor, this);
    }

    /* NB stats.is_up may be 0, there's no guarantee we're up because of
     * the way libasox does auto-connect-on-send */

    stat_incr(proxy, server_down_events_stat, 1);
    if (stats.is_up) {
      stat_decr(proxy, num_servers_up_stat, 1);
    }
    stats.is_up = 0;
    gettimeofday(&down_time, 0);

    /* Record on_down as a mc_res_connect_error; note we pass failure_until_tko
       to force TKO for the deprecated per-proxy logic */
    handle_tko(mc_res_connect_error, nullptr,
               proxy->opts.failures_until_tko);
  }
}

size_t ProxyDestination::getPendingRequestCount() const {
  return client_->getPendingRequestCount();
}

size_t ProxyDestination::getInflightRequestCount() const {
  return client_->getInflightRequestCount();
}

void ProxyDestination::reset_fields() {
  FBI_ASSERT(!proxy || proxy->magic == proxy_magic);
  if (!proxy || proxy->opts.disable_tko_tracking) {
    return;
  }

  /* Reset TKO state */
  consecutiveErrors_ = 0;
  if (proxy->router &&
      proxy->router->opts.global_tko_tracking) {
    if (shared && sending_probes) {
      unmark_global_tko();
    }
  } else {
    unmark_tko();
  }
}

std::shared_ptr<ProxyDestination> ProxyDestination::create(
    proxy_t* proxy,
    const ProxyClientCommon& ro,
    std::string pdstnKey) {

  auto ptr = std::shared_ptr<ProxyDestination>(
    new ProxyDestination(proxy, ro, std::move(pdstnKey)));
  ptr->selfPtr_ = ptr;
  ptr->client_ = folly::make_unique<DestinationMcClient>(ptr);
  return ptr;
}

ProxyDestination::~ProxyDestination() {
  if (proxy == nullptr) {
    // created for a unit test
    return;
  }

  if (owner != nullptr) {
    std::lock_guard<std::mutex> lock(owner->mx);
    shared->pclients.erase(this);
  }

  if (proxy->monitor) {
    proxy->monitor->remove_client(proxy->monitor, this);
  }

  client_.reset();

  if (stats_ptr) {
    dynamic_stats_unregister(stats_ptr);
  }

  if (stats.rtt_timer) {
    fb_timer_free(stats.rtt_timer);
  }

  if (sending_probes) {
    stop_sending_probes();
  }

  magic = kDeadBeef;
}

ProxyDestination::ProxyDestination(proxy_t* proxy_,
                                   const ProxyClientCommon& ro_,
                                   std::string pdstnKey_)
  : proxy(proxy_),
    accessPoint(ro_.ap),
    destinationKey(ro_.destination_key),
    rxpriority(ro_.rxpriority),
    txpriority(ro_.txpriority),
    server_timeout(ro_.server_timeout),
    pdstnKey(std::move(pdstnKey_)),
    use_ssl(ro_.useSsl) {

  static uint64_t next_magic = 0x12345678900000LL;
  magic = __sync_fetch_and_add(&next_magic, 1);

  proxy_magic = proxy->magic;

  if (!proxy->opts.disable_dynamic_stats) {
    nstring_t name_rtt = NSTRING_LIT("server_rtt");
    stats.rtt_timer = fb_timer_alloc(name_rtt, 0, 0);
    if (stats.rtt_timer == nullptr) {
      throw std::runtime_error("fb_timer_alloc of rtt_timer failure");
    }
  }

  if (!proxy->opts.disable_dynamic_stats &&
      !proxy->opts.disable_global_dynamic_stats) {
    stat_t stat;
    stat.name = pdstnKey;
    stat.group = server_stats;
    stat.type = stat_string_fn;
    stat.data.string_fn = proxy_client_stat_to_str;
    stat.size = dynamic_stat_size();
    stats_ptr = dynamic_stats_register(&stat, this);
    if (stats_ptr == nullptr) {
      throw std::runtime_error("dynamic_stats_register failure");
    }
  } else {
    FBI_ASSERT(stats_ptr == nullptr);
  }
}

/** Returns one of the three states that the server could be in:
 *  up, down, or total knockout (tko): means we're out for the count,
 *  i.e. we had a timeout or connection failure and haven't had time
 *  to recover.
 */
proxy_client_state_t ProxyDestination::state() {
  // We don't need to check if global tracking is disabled. If it is
  // isTko will always be false
  if ((shared && shared->tko.isTko()) || marked_tko) {
    return PROXY_CLIENT_TKO;
  } else if (stats.is_up) {
    return PROXY_CLIENT_UP;
  } else {
    return PROXY_CLIENT_NEW;
  }
}

int ProxyDestination::may_send(mc_msg_t *req) {
  FBI_ASSERT(!proxy || proxy->magic == proxy_magic);

  int rv;
  if (proxy && proxy->monitor) {
    rv = proxy->monitor->may_send(proxy->monitor,
                                  this, req);
  } else {
    rv = state() != PROXY_CLIENT_TKO;
  }
  return rv;
}

void ProxyDestination::sendFakeReply(mc_msg_t* request, void* req_ctx) {
  mc_res_t result;
  std::string replyStr;
  switch (request->op) {
    case mc_op_get:
      replyStr = "VERYRANDOMSTRING";
      result = mc_res_found;
      break;
    case mc_op_set:
      result = mc_res_stored;
      break;
    case mc_op_delete:
      result = mc_res_deleted;
      break;
    default:
      result = mc_res_ok;
  }

  auto reply = create_reply(request->op, result, replyStr.data());
  on_reply(request, reply, result, req_ctx);
}

int ProxyDestination::send(mc_msg_t* request, void* req_ctx,
                           uint64_t senderId) {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(request);

  proxy->destinationMap->markAsActive(*this);

  if (sample_key.empty() && request->key.len > 0) {
    FBI_ASSERT(request->key.str != nullptr);
    sample_key = to<std::string>(request->key);
  }

  // Do not communicate with memcache at all
  if (UNLIKELY(proxy->opts.no_network)) {
    sendFakeReply(request, req_ctx);
    return 0;
  }

  return client_->send(request, req_ctx, senderId);
}

void ProxyDestination::resetInactive() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(proxy);

  resetting = 1;
  client_->resetInactive();
  reset_fields();
  resetting = 0;
}

proxy_client_conn_stats_t::proxy_client_conn_stats_t()
    : is_up(0),
      rtt_timer(nullptr) {
  memset(results, 0, sizeof(results));
}

}}}  // facebook::memcache::mcrouter
