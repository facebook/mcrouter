/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyDestination.h"

#include <folly/Conv.h>
#include <folly/Memory.h>

#include "mcrouter/_router.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/fbi/util.h"
#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/pclient.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/routes/DestinationRoute.h"
#include "mcrouter/stats.h"
#include "mcrouter/TkoTracker.h"

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

bool is_error_reply(const McReply& reply) {
  if (reply.result() == mc_res_remote_error) {
    // mc_res_remote_error with a reply object is an application-level
    // error, not a network/server level error.
    return reply.isError() && reply.appSpecificErrorCode() == 0 &&
           reply.value().length() == 0;
  } else if (reply.result() == mc_res_try_again) {
    return false;
  } else {
    return mc_res_is_err(reply.result());
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
    if (probe_req.get() == nullptr) {
      auto mutReq = createMcMsgRef();
      mutReq->op = mc_op_version;
      probe_req = std::move(mutReq);
      ++probesSent_;
      send(probe_req.clone(), nullptr, /* senderId= */ 0);
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
  probesSent_ = 0;
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

bool ProxyDestination::is_hard_error(mc_res_t result) {
  switch (result) {
    case mc_res_connect_error:
    case mc_res_connect_timeout:
      return true;

    default:
      return false;
  }
}

void ProxyDestination::handle_tko(const McReply& reply,
                                  bool is_probe_req,
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
    if (is_error_reply(reply)) {
      if (is_hard_error(reply.result())) {
        responsible = shared->tko.recordHardFailure(this);
        if (responsible) {
          onTkoEvent(TkoLogEvent::MarkHardTko, reply.result());
        }
      } else {
        responsible = shared->tko.recordSoftFailure(this);
        if (responsible) {
          onTkoEvent(TkoLogEvent::MarkSoftTko, reply.result());
        }
      }
    } else if (proxy->opts.latency_threshold_us != 0 &&
               !sending_probes &&
               stats_.avgLatency.value() > proxy->opts.latency_threshold_us) {
    /* Even if it's not an error, if we've gone above our latency SLA we count
       that as a soft failure. We also check current latency to ensure that
       if things get better we don't keep TKOing the box */
      responsible = shared->tko.recordSoftFailure(this);
      if (responsible) {
        onTkoEvent(TkoLogEvent::MarkLatencyTko, reply.result());
      }
    } else {
      /* If we're sending probes, only a probe request should be considered
         successful to avoid outstanding requests from unmarking the box */
      if (!sending_probes || is_probe_req) {
        shared->tko.recordSuccess(this);
        if (sending_probes) {
          onTkoEvent(TkoLogEvent::UnMarkTko, reply.result());
          stop_sending_probes();
        }
      }
    }
    if (responsible) {
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

void ProxyDestination::on_reply(const McMsgRef& req,
                                McReply reply,
                                void* req_ctx) {
  FBI_ASSERT(proxy->magic == proxy_magic);

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
  if (is_error_reply(reply)) {
    ++consecutiveErrors_;
  } else {
    consecutiveErrors_ = 0;
    /**
     * HACK: It's possible that we missed an on_up callback for this pdstn
     * if it happened right during a reconfigure request, but a successful
     * on_reply was still delivered.  So we just manually call on_up here.
     * Ideally we wouldn't miss on_up callbacks, but it's not easy to do.
     */
    if (!stats_.is_up) {
      on_up();
    }
  }

  bool is_probe_req = (req.get() == probe_req.get());

  if (proxy->monitor) {
      proxy->monitor->on_response(proxy->monitor, this,
                                  const_cast<mc_msg_t*>(req.get()),
                                  reply);
  } else {
    handle_tko(reply, is_probe_req, consecutiveErrors_);
  }

  if (is_probe_req) {
    probe_req = McMsgRef();
  } else {
    stats_.results[reply.result()]++;

    auto destreqCtx = reinterpret_cast<DestinationRequestCtx*>(req_ctx);
    preq = destreqCtx->preq;

    destreqCtx->endTime = nowUs();
    destreqCtx->reply = std::move(reply);

    /* For code simplicity we look at latency for making TKO decisions with a
       1 request delay */
    int64_t latency = destreqCtx->endTime - destreqCtx->startTime;
    stats_.avgLatency.insertSample(latency);

    auto promise = std::move(destreqCtx->promise.value());
    promise.setValue();
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
  FBI_ASSERT(!stats_.is_up);

  stat_incr(proxy, server_up_events_stat, 1);
  stat_incr(proxy, num_servers_up_stat, 1);

  stats_.is_up = true;
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
    if (stats_.is_up) {
      stat_decr(proxy, num_servers_up_stat, 1);
      stats_.is_up = false;
    }
  } else {
    VLOG(1) << "server " << pdstnKey << " down (" <<
        stat_get_uint64(proxy, num_servers_up_stat) << " of " <<
        stat_get_uint64(proxy, num_servers_stat) << ")";

    if (proxy->monitor) {
      proxy->monitor->on_down(proxy->monitor, this);
    }

    /* NB stats_.is_up may be 0, there's no guarantee we're up because of
     * the way libasox does auto-connect-on-send */

    stat_incr(proxy, server_down_events_stat, 1);
    if (stats_.is_up) {
      stat_decr(proxy, num_servers_up_stat, 1);
    }
    stats_.is_up = false;
    gettimeofday(&down_time, 0);

    /* Record on_down as a mc_res_connect_error; note we pass failure_until_tko
       to force TKO for the deprecated per-proxy logic */
    handle_tko(McReply(mc_res_connect_error),
               /* is_probe_req= */ false,
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
  /* This all goes away once global TKO is rolled out */
  FBI_ASSERT(!proxy || proxy->magic == proxy_magic);
  if (!proxy || proxy->opts.disable_tko_tracking) {
    return;
  }

  /* Reset TKO state */
  consecutiveErrors_ = 0;

  if (!proxy->router ||
      !proxy->router->opts.global_tko_tracking) {
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
    shared->pdstns.erase(this);
  }

  if (proxy->monitor) {
    proxy->monitor->remove_client(proxy->monitor, this);
  }

  client_.reset();

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
    proxy_magic(proxy->magic),
    use_ssl(ro_.useSsl),
    stats_(proxy_->opts) {

  static uint64_t next_magic = 0x12345678900000LL;
  magic = __sync_fetch_and_add(&next_magic, 1);
}

proxy_client_state_t ProxyDestination::state() const {
  // We don't need to check if global tracking is disabled. If it is
  // isTko will always be false
  if ((shared && shared->tko.isTko()) || marked_tko) {
    return PROXY_CLIENT_TKO;
  } else if (stats_.is_up) {
    return PROXY_CLIENT_UP;
  } else {
    return PROXY_CLIENT_NEW;
  }
}

const ProxyDestinationStats& ProxyDestination::stats() const {
  return stats_;
}

int ProxyDestination::may_send(const McMsgRef& req) {
  FBI_ASSERT(!proxy || proxy->magic == proxy_magic);

  int rv;
  if (proxy && proxy->monitor) {
    rv = proxy->monitor->may_send(proxy->monitor,
                                  this, const_cast<mc_msg_t*>(req.get()));
  } else {
    rv = state() != PROXY_CLIENT_TKO;
  }
  return rv;
}

void ProxyDestination::sendFakeReply(const McMsgRef& request, void* req_ctx) {
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

  on_reply(request, McReply(result, replyStr), req_ctx);
}

int ProxyDestination::send(McMsgRef request, void* req_ctx,
                           uint64_t senderId) {
  FBI_ASSERT(proxy->magic == proxy_magic);

  proxy->destinationMap->markAsActive(*this);

  // Do not communicate with memcache at all
  if (UNLIKELY(proxy->opts.no_network)) {
    sendFakeReply(request, req_ctx);
    return 0;
  }

  return client_->send(std::move(request), req_ctx, senderId);
}

void ProxyDestination::resetInactive() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(proxy);

  resetting = 1;
  client_->resetInactive();
  reset_fields();
  resetting = 0;
}

void ProxyDestination::onTkoEvent(TkoLogEvent event, mc_res_t result) const {
  if (!shared) {
    return;
  }

  switch (event) {
    case TkoLogEvent::MarkHardTko:
      VLOG(1) << shared->key << " marked hard TKO. Reply: "
              << mc_res_to_string(result);
      break;
    case TkoLogEvent::MarkSoftTko:
      VLOG(1) << shared->key << " marked soft TKO. Reply: "
              << mc_res_to_string(result);
      break;
    case TkoLogEvent::MarkLatencyTko:
      VLOG(1) << shared->key << " marked latency TKO. Reply: "
              << mc_res_to_string(result);
      break;
    case TkoLogEvent::UnMarkTko:
      VLOG(1) << shared->key << " unmarked TKO. Reply: "
              << mc_res_to_string(result);
      break;
  }

  TkoLog tkoLog(accessPoint);
  tkoLog.event = event;
  tkoLog.globalSoftTkos = shared->tko.globalSoftTkos();
  tkoLog.isHardTko = shared->tko.isHardTko();
  tkoLog.isSoftTko = shared->tko.isSoftTko();
  tkoLog.avgLatency = stats_.avgLatency.value();
  tkoLog.probesSent = probesSent_;
  tkoLog.result = result;

  logTkoEvent(proxy, tkoLog);
}

ProxyDestinationStats::ProxyDestinationStats(const McrouterOptions& opts)
  : avgLatency(1.0 / opts.latency_window_size) {
}

}}}  // facebook::memcache::mcrouter
