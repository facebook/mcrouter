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

void ProxyDestination::unmark_tko(const McReply& reply) {
  FBI_ASSERT(!proxy->opts.disable_tko_tracking);
  shared->tko.recordSuccess(this);
  if (sending_probes) {
    onTkoEvent(TkoLogEvent::UnMarkTko, reply.result());
    stop_sending_probes();
  }
}

void ProxyDestination::handle_tko(const McReply& reply, bool is_probe_req) {
  if (resetting ||
      proxy->opts.disable_tko_tracking) {
    return;
  }

  bool responsible = false;
  if (reply.isError()) {
    if (reply.isHardTkoError()) {
      responsible = shared->tko.recordHardFailure(this);
      if (responsible) {
        onTkoEvent(TkoLogEvent::MarkHardTko, reply.result());
      }
    } else if (reply.isSoftTkoError()) {
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
  /* If we're sending probes, only a probe request should be considered
     successful to avoid outstanding requests from unmarking the box */
  } else if (!sending_probes || is_probe_req) {
    unmark_tko(reply);
  }
  if (responsible) {
    start_sending_probes();
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

  bool is_probe_req = (req.get() == probe_req.get());

  handle_tko(reply, is_probe_req);

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
    stat_decr(proxy->stats, sum_server_queue_length_stat, 1);
  }

  if (!proxy->opts.disable_dynamic_stats) {
    fb_timer_finish(on_reply_timer);
  }
}

void ProxyDestination::on_up() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(!stats_.is_up);

  stat_incr(proxy->stats, server_up_events_stat, 1);
  stat_incr(proxy->stats, num_servers_up_stat, 1);

  stats_.is_up = true;

  VLOG(1) << "server " << pdstnKey << " up (" <<
      stat_get_uint64(proxy->stats, num_servers_up_stat) << " of " <<
      stat_get_uint64(proxy->stats, num_servers_stat) << ")";
}

void ProxyDestination::on_down() {
  FBI_ASSERT(proxy->magic == proxy_magic);

  if (resetting) {
    VLOG(1) << "server " << pdstnKey << " inactive (" <<
        stat_get_uint64(proxy->stats, num_servers_up_stat) << " of " <<
        stat_get_uint64(proxy->stats, num_servers_stat) << ")";
    stat_incr(proxy->stats, closed_inactive_connections_stat, 1);
    if (stats_.is_up) {
      stat_decr(proxy->stats, num_servers_up_stat, 1);
      stats_.is_up = false;
    }
  } else {
    VLOG(1) << "server " << pdstnKey << " down (" <<
        stat_get_uint64(proxy->stats, num_servers_up_stat) << " of " <<
        stat_get_uint64(proxy->stats, num_servers_stat) << ")";

    /* NB stats_.is_up may be 0, there's no guarantee we're up because of
     * the way libasox does auto-connect-on-send */

    stat_incr(proxy->stats, server_down_events_stat, 1);
    if (stats_.is_up) {
      stat_decr(proxy->stats, num_servers_up_stat, 1);
    }
    stats_.is_up = false;

    handle_tko(McReply(mc_res_connect_error),
               /* is_probe_req= */ false);
  }
}

size_t ProxyDestination::getPendingRequestCount() const {
  return client_->getPendingRequestCount();
}

size_t ProxyDestination::getInflightRequestCount() const {
  return client_->getInflightRequestCount();
}

std::pair<uint64_t, uint64_t> ProxyDestination::getBatchingStat() const {
  return client_->getBatchingStat();
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
    qos(ro_.qos),
    stats_(proxy_->opts) {

  static uint64_t next_magic = 0x12345678900000LL;
  magic = __sync_fetch_and_add(&next_magic, 1);
}

proxy_client_state_t ProxyDestination::state() const {
  if (shared->tko.isTko()) {
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

  return state() != PROXY_CLIENT_TKO;
}

int ProxyDestination::send(McMsgRef request, void* req_ctx,
                           uint64_t senderId) {
  FBI_ASSERT(proxy->magic == proxy_magic);

  proxy->destinationMap->markAsActive(*this);

  return client_->send(std::move(request), req_ctx, senderId);
}

void ProxyDestination::resetInactive() {
  FBI_ASSERT(proxy->magic == proxy_magic);
  FBI_ASSERT(proxy);

  resetting = 1;
  client_->resetInactive();
  resetting = 0;
}

void ProxyDestination::onTkoEvent(TkoLogEvent event, mc_res_t result) const {
  auto logUtil = [this, result](folly::StringPiece eventStr) {
    VLOG(1) << shared->key << " " << eventStr << " TKO. Total hard TKOs: "
            << shared->tko.globalTkos().hardTkos << "; soft TKOs: "
            << shared->tko.globalTkos().softTkos << ". Reply: "
            << mc_res_to_string(result);
  };

  switch (event) {
    case TkoLogEvent::MarkHardTko:
      logUtil("marked hard");
      break;
    case TkoLogEvent::MarkSoftTko:
      logUtil("marked soft");
      break;
    case TkoLogEvent::MarkLatencyTko:
      logUtil("marked latency");
      break;
    case TkoLogEvent::UnMarkTko:
      logUtil("unmarked");
      break;
  }

  TkoLog tkoLog(accessPoint, shared->tko.globalTkos());
  tkoLog.event = event;
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
