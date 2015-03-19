/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyDestination.h"

#include <folly/Memory.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/ProxyClientCommon.h"
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

stat_name_t getStatName(ProxyDestination::State st) {
  switch (st) {
    case ProxyDestination::State::kNew:
      return num_servers_new_stat;
    case ProxyDestination::State::kUp:
      return num_servers_up_stat;
    case ProxyDestination::State::kClosed:
      return num_servers_closed_stat;
    case ProxyDestination::State::kDown:
      return num_servers_down_stat;
    case ProxyDestination::State::kNumStates:
      CHECK(false);
  }
  return num_stats; // shouldn't reach here
}

}  // anonymous namespace

void ProxyDestination::schedule_next_probe() {
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
  probe_timer = asox_add_timer(
    proxy->eventBase->getLibeventBase(),
    delay,
    [](const asox_timer_t timer, void* arg) {
      reinterpret_cast<ProxyDestination*>(arg)->on_timer(timer);
    }, this);
}

void ProxyDestination::on_timer(const asox_timer_t timer) {
  // This assert checks for use-after-free
  FBI_ASSERT(timer == probe_timer);
  asox_remove_timer(timer);
  probe_timer = nullptr;
  if (sending_probes) {
    // Note that the previous probe might still be in flight
    if (!probe_req) {
      auto mutReq = createMcMsgRef();
      mutReq->op = mc_op_version;
      probe_req = folly::make_unique<McRequest>(std::move(mutReq));
      ++stats_.probesSent;
      auto selfPtr = selfPtr_;
      proxy->fiberManager.addTask([selfPtr]() mutable {
        auto pdstn = selfPtr.lock();
        if (pdstn == nullptr) {
          return;
        }
        pdstn->proxy->destinationMap->markAsActive(*pdstn);
        // will reconnect if connection was closed
        McReply reply = pdstn->getAsyncMcClient().sendSync(
          *pdstn->probe_req,
          McOperation<mc_op_version>(),
          pdstn->shortestTimeout_);
        pdstn->handle_tko(reply, true);
        pdstn->probe_req.reset();
      });
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
  stats_.probesSent = 0;
  sending_probes = false;
  if (probe_timer) {
    asox_remove_timer(probe_timer);
    probe_timer = nullptr;
  }
}

void ProxyDestination::unmark_tko(const McReply& reply) {
  FBI_ASSERT(!proxy->opts.disable_tko_tracking);
  tracker->recordSuccess(this);
  if (sending_probes) {
    onTkoEvent(TkoLogEvent::UnMarkTko, reply.result());
    stop_sending_probes();
  }
}

void ProxyDestination::handle_tko(const McReply& reply, bool is_probe_req) {
  if (resetting || proxy->opts.disable_tko_tracking) {
    return;
  }

  bool responsible = false;
  if (reply.isError()) {
    if (reply.isHardTkoError()) {
      responsible = tracker->recordHardFailure(this);
      if (responsible) {
        onTkoEvent(TkoLogEvent::MarkHardTko, reply.result());
      }
    } else if (reply.isSoftTkoError()) {
      responsible = tracker->recordSoftFailure(this);
      if (responsible) {
        onTkoEvent(TkoLogEvent::MarkSoftTko, reply.result());
      }
    }
  } else if (!sending_probes || is_probe_req) {
    /* If we're sending probes, only a probe request should be considered
       successful to avoid outstanding requests from unmarking the box */
    unmark_tko(reply);
  }
  if (responsible) {
    start_sending_probes();
  }
}

void ProxyDestination::onReply(const McReply& reply,
                               DestinationRequestCtx& destreqCtx) {
  handle_tko(reply, false);

  stats_.results[reply.result()]++;
  destreqCtx.endTime = nowUs();

  int64_t latency = destreqCtx.endTime - destreqCtx.startTime;
  stats_.avgLatency.insertSample(latency);
}

size_t ProxyDestination::getPendingRequestCount() const {
  return client_ ? client_->getPendingRequestCount() : 0;
}

size_t ProxyDestination::getInflightRequestCount() const {
  return client_ ? client_->getInflightRequestCount() : 0;
}

std::pair<uint64_t, uint64_t> ProxyDestination::getBatchingStat() const {
  return client_ ? client_->getBatchingStat() : std::make_pair(0UL, 0UL);
}

std::shared_ptr<ProxyDestination> ProxyDestination::create(
    proxy_t* proxy,
    const ProxyClientCommon& ro,
    std::string pdstnKey) {

  auto ptr = std::shared_ptr<ProxyDestination>(
    new ProxyDestination(proxy, ro, std::move(pdstnKey)));
  ptr->selfPtr_ = ptr;
  return ptr;
}

ProxyDestination::~ProxyDestination() {
  if (sending_probes) {
    onTkoEvent(TkoLogEvent::RemoveFromConfig, mc_res_ok);
    stop_sending_probes();
  }
  tracker->removeDestination(this);

  if (proxy->destinationMap) {
    proxy->destinationMap->removeDestination(*this);
  }

  if (client_) {
    client_->setStatusCallbacks(nullptr, nullptr);
    client_->closeNow();
  }

  stat_decr(proxy->stats, getStatName(stats_.state), 1);
  magic_ = kDeadBeef;
}

ProxyDestination::ProxyDestination(proxy_t* proxy_,
                                   const ProxyClientCommon& ro_,
                                   std::string pdstnKey_)
  : proxy(proxy_),
    accessPoint(ro_.ap),
    pdstnKey(std::move(pdstnKey_)),
    shortestTimeout_(ro_.server_timeout),
    useSsl_(ro_.useSsl),
    qos_(ro_.qos),
    stats_(proxy_->opts),
    poolName_(ro_.pool.getName()) {

  static uint64_t next_magic = 0x12345678900000LL;
  magic_ = __sync_fetch_and_add(&next_magic, 1);
  stat_incr(proxy->stats, num_servers_new_stat, 1);
}

bool ProxyDestination::may_send() const {
  return !tracker->isTko();
}

void ProxyDestination::resetInactive() {
  // No need to reset non-existing client.
  if (client_) {
    resetting = 1;
    client_->closeNow();
    client_.reset();
    resetting = 0;
  }
}

void ProxyDestination::initializeAsyncMcClient() {
  CHECK(proxy->eventBase);
  assert(!client_);

  ConnectionOptions options(accessPoint);
  auto& opts = proxy->opts;
  options.noNetwork = opts.no_network;
  options.tcpKeepAliveCount = opts.keepalive_cnt;
  options.tcpKeepAliveIdle = opts.keepalive_idle_s;
  options.tcpKeepAliveInterval = opts.keepalive_interval_s;
  options.writeTimeout = shortestTimeout_;
  if (proxy->opts.enable_qos) {
    options.enableQoS = true;
    options.qos = qos_;
  }

  if (useSsl_) {
    checkLogic(!opts.pem_cert_path.empty() &&
               !opts.pem_key_path.empty() &&
               !opts.pem_ca_path.empty(),
               "Some of ssl key paths are not set!");
    options.sslContextProvider = [&opts] {
      return getSSLContext(opts.pem_cert_path, opts.pem_key_path,
                           opts.pem_ca_path);
    };
  }

  client_ = folly::make_unique<AsyncMcClient>(*proxy->eventBase,
                                              std::move(options));

  client_->setStatusCallbacks(
    [this] () mutable {
      setState(State::kUp);
    },
    [this] (const folly::AsyncSocketException&) mutable {
      if (resetting) {
        setState(State::kClosed);
      } else {
        setState(State::kDown);
        handle_tko(McReply(mc_res_connect_error), /* is_probe_req= */ false);
      }
    });

  if (opts.target_max_inflight_requests > 0) {
    client_->setThrottle(opts.target_max_inflight_requests,
                         opts.target_max_pending_requests);
  }
}

AsyncMcClient& ProxyDestination::getAsyncMcClient() {
  if (!client_) {
    initializeAsyncMcClient();
  }
  return *client_;
}

void ProxyDestination::onTkoEvent(TkoLogEvent event, mc_res_t result) const {
  auto logUtil = [this, result](folly::StringPiece eventStr) {
    VLOG(1) << accessPoint.toHostPortString() << " (" << poolName_ << ") "
            << eventStr << ". Total hard TKOs: "
            << tracker->globalTkos().hardTkos << "; soft TKOs: "
            << tracker->globalTkos().softTkos << ". Reply: "
            << mc_res_to_string(result);
  };

  switch (event) {
    case TkoLogEvent::MarkHardTko:
      logUtil("marked hard TKO");
      break;
    case TkoLogEvent::MarkSoftTko:
      logUtil("marked soft TKO");
      break;
    case TkoLogEvent::UnMarkTko:
      logUtil("unmarked TKO");
      break;
    case TkoLogEvent::RemoveFromConfig:
      logUtil("was TKO, removed from config");
      break;
  }

  TkoLog tkoLog(accessPoint, tracker->globalTkos());
  tkoLog.event = event;
  tkoLog.isHardTko = tracker->isHardTko();
  tkoLog.isSoftTko = tracker->isSoftTko();
  tkoLog.avgLatency = stats_.avgLatency.value();
  tkoLog.probesSent = stats_.probesSent;
  tkoLog.poolName = poolName_;
  tkoLog.result = result;

  logTkoEvent(proxy, tkoLog);
}

void ProxyDestination::setState(State new_st) {
  if (stats_.state == new_st) {
    return;
  }

  auto logUtil = [this](const char* s) {
    VLOG(1) << "server " << pdstnKey << " " << s << " (" <<
        stat_get_uint64(proxy->stats, num_servers_up_stat) << " of " <<
        stat_get_uint64(proxy->stats, num_servers_stat) << ")";
  };

  auto old_name = getStatName(stats_.state);
  auto new_name = getStatName(new_st);
  stat_decr(proxy->stats, old_name, 1);
  stat_incr(proxy->stats, new_name, 1);
  stats_.state = new_st;

  switch (stats_.state) {
    case State::kUp:
      logUtil("up");
      break;
    case State::kClosed:
      logUtil("inactive");
      break;
    case State::kDown:
      logUtil("down");
      break;
    case State::kNew:
    case State::kNumStates:
      assert(false);
      break;
  }
}

void ProxyDestination::updateShortestTimeout(
    std::chrono::milliseconds timeout) {
  if (!timeout.count()) {
    return;
  }
  if (shortestTimeout_.count() == 0 || shortestTimeout_ > timeout) {
    shortestTimeout_ = timeout;
    if (client_) {
      client_->updateWriteTimeout(shortestTimeout_);
    }
  }
}

ProxyDestination::Stats::Stats(const McrouterOptions& opts)
  : avgLatency(1.0 / opts.latency_window_size) {
}

}}}  // facebook::memcache::mcrouter
