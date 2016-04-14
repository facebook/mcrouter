/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyDestination.h"

#include <folly/experimental/fibers/Fiber.h>
#include <folly/Memory.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/OptionsUtil.h"
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
  assert(!proxy->router().opts().disable_tko_tracking);

  int delay_ms = probe_delay_next_ms;
  if (probe_delay_next_ms < 2) {
    // int(1 * 1.5) == 1, so advance it to 2 first
    probe_delay_next_ms = 2;
  } else {
    probe_delay_next_ms *= kProbeExponentialFactor;
  }
  if (probe_delay_next_ms > proxy->router().opts().probe_delay_max_ms) {
    probe_delay_next_ms = proxy->router().opts().probe_delay_max_ms;
  }

  // Calculate random jitter
  double r = (double)rand() / (double)RAND_MAX;
  double tmo_jitter_pct = r * kProbeJitterDelta + kProbeJitterMin;
  uint64_t delay_us = (double)delay_ms * 1000 * (1.0 + tmo_jitter_pct);
  assert(delay_us > 0);

  timeval_t delay;
  delay.tv_sec = (delay_us / 1000000);
  delay.tv_usec = (delay_us % 1000000);

  assert(probe_timer == nullptr);
  probe_timer = asox_add_timer(
    proxy->eventBase().getLibeventBase(),
    delay,
    [](const asox_timer_t timer, void* arg) {
      reinterpret_cast<ProxyDestination*>(arg)->on_timer(timer);
    }, this);
}

void ProxyDestination::on_timer(const asox_timer_t timer) {
  // This assert checks for use-after-free
  assert(timer == probe_timer);
  asox_remove_timer(timer);
  probe_timer = nullptr;
  // Note that the previous probe might still be in flight
  if (!probe_req) {
    probe_req = folly::make_unique<McRequestWithMcOp<mc_op_version>>();
    ++stats_.probesSent;
    auto selfPtr = selfPtr_;
    proxy->fiberManager.addTask([selfPtr]() mutable {
      auto pdstn = selfPtr.lock();
      if (pdstn == nullptr) {
        return;
      }
      pdstn->proxy->destinationMap->markAsActive(*pdstn);
      // will reconnect if connection was closed
      auto reply = pdstn->getAsyncMcClient().sendSync(
        *pdstn->probe_req,
        pdstn->shortestTimeout_);
      pdstn->handle_tko(reply.result(), true);
      pdstn->probe_req.reset();
    });
  }
  schedule_next_probe();
}

void ProxyDestination::start_sending_probes() {
  probe_delay_next_ms = proxy->router().opts().probe_delay_initial_ms;
  schedule_next_probe();
}

void ProxyDestination::stop_sending_probes() {
  stats_.probesSent = 0;
  if (probe_timer) {
    asox_remove_timer(probe_timer);
    probe_timer = nullptr;
  }
}

void ProxyDestination::handle_tko(const mc_res_t result, bool is_probe_req) {
  if (proxy->router().opts().disable_tko_tracking) {
    return;
  }

  if (isErrorResult(result)) {
    if (isHardTkoErrorResult(result)) {
      if (tracker->recordHardFailure(this)) {
        onTkoEvent(TkoLogEvent::MarkHardTko, result);
        start_sending_probes();
      }
    } else if (isSoftTkoErrorResult(result)) {
      if (tracker->recordSoftFailure(this)) {
        onTkoEvent(TkoLogEvent::MarkSoftTko, result);
        start_sending_probes();
      }
    }
    return;
  }

  if (tracker->isTko()) {
    if (is_probe_req && tracker->recordSuccess(this)) {
      onTkoEvent(TkoLogEvent::UnMarkTko, result);
      stop_sending_probes();
    }
    return;
  }

  tracker->recordSuccess(this);
}

void ProxyDestination::onReply(const mc_res_t result,
                               DestinationRequestCtx& destreqCtx) {
  handle_tko(result, false);

  if (!stats_.results) {
    stats_.results = folly::make_unique<std::array<uint64_t, mc_nres>>();
  }
  ++(*stats_.results)[result];
  destreqCtx.endTime = nowUs();

  int64_t latency = destreqCtx.endTime - destreqCtx.startTime;
  stats_.avgLatency.insertSample(latency);
}

size_t ProxyDestination::getPendingRequestCount() const {
  folly::SpinLockGuard g(clientLock_);
  return client_ ? client_->getPendingRequestCount() : 0;
}

size_t ProxyDestination::getInflightRequestCount() const {
  folly::SpinLockGuard g(clientLock_);
  return client_ ? client_->getInflightRequestCount() : 0;
}

std::shared_ptr<ProxyDestination> ProxyDestination::create(
    proxy_t& proxy,
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath) {

  std::shared_ptr<ProxyDestination> ptr(new ProxyDestination(
    proxy,
    std::move(ap),
    timeout,
    qosClass,
    qosPath));
  ptr->selfPtr_ = ptr;
  return ptr;
}

ProxyDestination::~ProxyDestination() {
  if (tracker->removeDestination(this)) {
    onTkoEvent(TkoLogEvent::RemoveFromConfig, mc_res_ok);
    stop_sending_probes();
  }

  if (proxy->destinationMap) {
    proxy->destinationMap->removeDestination(*this);
  }

  if (client_) {
    client_->setStatusCallbacks(nullptr, nullptr);
    client_->closeNow();
  }

  stat_decr(proxy->stats, getStatName(stats_.state), 1);
  stat_decr(proxy->stats, num_servers_stat, 1);
  magic_ = kDeadBeef;
}

ProxyDestination::ProxyDestination(
  proxy_t& proxy_,
  std::shared_ptr<AccessPoint> ap,
  std::chrono::milliseconds timeout,
  uint64_t qosClass,
  uint64_t qosPath)
    : proxy(&proxy_),
      accessPoint_(std::move(ap)),
      shortestTimeout_(timeout),
      qosClass_(qosClass),
      qosPath_(qosPath) {

  static uint64_t next_magic = 0x12345678900000LL;
  magic_ = __sync_fetch_and_add(&next_magic, 1);
  stat_incr(proxy->stats, num_servers_new_stat, 1);
  stat_incr(proxy->stats, num_servers_stat, 1);
}

bool ProxyDestination::may_send() const {
  return !tracker->isTko();
}

void ProxyDestination::resetInactive() {
  // No need to reset non-existing client.
  if (client_) {
    std::unique_ptr<AsyncMcClient> client;
    {
      folly::SpinLockGuard g(clientLock_);
      client = std::move(client_);
    }
    client->closeNow();
  }
}

void ProxyDestination::initializeAsyncMcClient() {
  assert(!client_);

  ConnectionOptions options(accessPoint_);
  auto& opts = proxy->router().opts();
  options.noNetwork = opts.no_network;
  options.tcpKeepAliveCount = opts.keepalive_cnt;
  options.tcpKeepAliveIdle = opts.keepalive_idle_s;
  options.tcpKeepAliveInterval = opts.keepalive_interval_s;
  options.writeTimeout = shortestTimeout_;
  options.sessionCachingEnabled = opts.ssl_connection_cache;
  if (!opts.debug_fifo_root.empty()) {
    options.debugFifoPath = getClientDebugFifoFullPath(opts);
  }
  if (opts.enable_qos) {
    options.enableQoS = true;
    options.qosClass = qosClass_;
    options.qosPath = qosPath_;
  }
  options.useJemallocNodumpAllocator = opts.jemalloc_nodump_buffers;

  if (accessPoint_->useSsl()) {
    checkLogic(!opts.pem_cert_path.empty() &&
               !opts.pem_key_path.empty() &&
               !opts.pem_ca_path.empty(),
               "Some of ssl key paths are not set!");
    options.sslContextProvider = [&opts] {
      return getSSLContext(opts.pem_cert_path, opts.pem_key_path,
                           opts.pem_ca_path);
    };
  }

  auto client = folly::make_unique<AsyncMcClient>(proxy->eventBase(),
                                                  std::move(options));
  {
    folly::SpinLockGuard g(clientLock_);
    client_ = std::move(client);
  }

  client_->setRequestStatusCallbacks(
    [this](int pending, int inflight) {
      if (pending != 0) {
        stat_incr(proxy->stats, destination_pending_reqs_stat, pending);
        stat_set_uint64(
            proxy->stats,
            destination_max_pending_reqs_stat,
            std::max(stat_get_uint64(proxy->stats,
                                     destination_max_pending_reqs_stat),
                     stat_get_uint64(proxy->stats,
                                     destination_pending_reqs_stat)));
      }
      if (inflight != 0) {
        stat_incr(proxy->stats, destination_inflight_reqs_stat, inflight);
        stat_set_uint64(
            proxy->stats,
            destination_max_inflight_reqs_stat,
            std::max(stat_get_uint64(proxy->stats,
                                     destination_max_inflight_reqs_stat),
                     stat_get_uint64(proxy->stats,
                                     destination_inflight_reqs_stat)));
      }
    },
    [this](size_t numToSend) {
      stat_incr(proxy->stats, destination_batches_sum_stat, 1);
      stat_incr(proxy->stats, destination_requests_sum_stat, numToSend);
    });

  client_->setStatusCallbacks(
    [this] () mutable {
      setState(State::kUp);
    },
    [this] (bool aborting) mutable {
      if (aborting) {
        setState(State::kClosed);
      } else {
        setState(State::kDown);
        handle_tko(mc_res_connect_error, /* is_probe_req= */ false);
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
    VLOG(1) << accessPoint_->toHostPortString() << " (" << poolName_ << ") "
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

  TkoLog tkoLog(*accessPoint_, tracker->globalTkos());
  tkoLog.event = event;
  tkoLog.isHardTko = tracker->isHardTko();
  tkoLog.isSoftTko = tracker->isSoftTko();
  tkoLog.avgLatency = stats_.avgLatency.value();
  tkoLog.probesSent = stats_.probesSent;
  tkoLog.poolName = poolName_;
  tkoLog.result = result;

  logTkoEvent(*proxy, tkoLog);
}

void ProxyDestination::setState(State new_st) {
  if (stats_.state == new_st) {
    return;
  }

  auto logUtil = [this](const char* s) {
    VLOG(1) << "server " << pdstnKey_ << " " << s << " (" <<
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
      logUtil("closed");
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

}}}  // facebook::memcache::mcrouter
