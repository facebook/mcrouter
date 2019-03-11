/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "ProxyDestination.h"

#include <chrono>
#include <limits>
#include <memory>
#include <random>

#include <folly/fibers/Fiber.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncTimeout.h>

#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/OptionsUtil.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Clocks.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/McFizzClient.h"
#include "mcrouter/lib/network/RpcStatsContext.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/DestinationRoute.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

bool ProxyDestination::latencyAboveThreshold(uint64_t latency) {
  const auto rxmitDeviation =
      proxy().router().opts().rxmit_latency_deviation_us;
  if (!rxmitDeviation) {
    return false;
  }
  return (
      static_cast<double>(latency) - stats().avgLatency.value() >
      static_cast<double>(rxmitDeviation));
}

void ProxyDestination::handleRxmittingConnection(
    const carbon::Result result,
    uint64_t latency) {
  constexpr uint32_t kReconnectionHoldoffFactor = 25;
  if (!client_) {
    return;
  }
  const auto retransCycles =
      proxy().router().opts().collect_rxmit_stats_every_hz;
  if (retransCycles > 0 &&
      (isDataTimeoutResult(result) || latencyAboveThreshold(latency))) {
    const auto curCycles = cycles::getCpuCycles();
    if (curCycles > lastRetransCycles_ + retransCycles) {
      lastRetransCycles_ = curCycles;
      const auto currRetransPerKByte = client_->getRetransmissionInfo();
      if (currRetransPerKByte >= 0.0) {
        stats().retransPerKByte = currRetransPerKByte;
        proxy().stats().setValue(
            retrans_per_kbyte_max_stat,
            std::max(
                proxy().stats().getValue(retrans_per_kbyte_max_stat),
                static_cast<uint64_t>(currRetransPerKByte)));
        proxy().stats().increment(
            retrans_per_kbyte_sum_stat,
            static_cast<int64_t>(currRetransPerKByte));
        proxy().stats().increment(retrans_num_total_stat);
      }

      if (proxy().router().isRxmitReconnectionDisabled()) {
        return;
      }

      if (rxmitsToCloseConnection_ > 0 &&
          currRetransPerKByte >= rxmitsToCloseConnection_) {
        std::uniform_int_distribution<uint64_t> dist(
            1, kReconnectionHoldoffFactor);
        const uint64_t reconnectionJitters =
            retransCycles * dist(proxy().randomGenerator());
        if (lastConnCloseCycles_ + reconnectionJitters > curCycles) {
          return;
        }
        client_->closeNow();
        proxy().stats().increment(retrans_closed_connections_stat);
        lastConnCloseCycles_ = curCycles;

        const auto maxThreshold =
            proxy().router().opts().max_rxmit_reconnect_threshold;
        const uint64_t maxRxmitReconnThreshold = maxThreshold == 0
            ? std::numeric_limits<uint64_t>::max()
            : maxThreshold;
        rxmitsToCloseConnection_ =
            std::min(maxRxmitReconnThreshold, 2 * rxmitsToCloseConnection_);
      } else if (3 * currRetransPerKByte < rxmitsToCloseConnection_) {
        const auto minThreshold =
            proxy().router().opts().min_rxmit_reconnect_threshold;
        rxmitsToCloseConnection_ =
            std::max(minThreshold, rxmitsToCloseConnection_ / 2);
      }
    }
  }
}

void ProxyDestination::updateTransportTimeoutsIfShorter(
    std::chrono::milliseconds shortestConnectTimeout,
    std::chrono::milliseconds shortestWriteTimeout) {
  folly::SpinLockGuard g(clientLock_);
  if (client_) {
    client_->updateTimeoutsIfShorter(
        shortestConnectTimeout, shortestWriteTimeout);
  }
}

carbon::Result ProxyDestination::sendProbe() {
  // Will reconnect if connection was closed
  // Version commands shouldn't take much longer than stablishing a
  // connection, so just using shortestConnectTimeout() here.
  return getAsyncMcClient()
      .sendSync(McVersionRequest(), shortestWriteTimeout())
      .result();
}

void ProxyDestination::markAsActive() {
  proxy().destinationMap()->markAsActive(*this);
}

void ProxyDestination::onReply(
    const carbon::Result result,
    DestinationRequestCtx& destreqCtx,
    const RpcStatsContext& rpcStatsContext,
    bool isRequestBufferDirty) {
  handleTko(result, /* is_probe_req */ false);

  if (!stats().results) {
    stats().results = std::make_unique<std::array<
        uint64_t,
        static_cast<size_t>(carbon::Result::NUM_RESULTS)>>();
  }
  ++(*stats().results)[static_cast<size_t>(result)];
  destreqCtx.endTime = nowUs();

  int64_t latency = destreqCtx.endTime - destreqCtx.startTime;
  stats().avgLatency.insertSample(latency);

  if (accessPoint()->compressed()) {
    if (rpcStatsContext.usedCodecId > 0) {
      proxy().stats().increment(replies_compressed_stat);
    } else {
      proxy().stats().increment(replies_not_compressed_stat);
    }
    proxy().stats().increment(
        reply_traffic_before_compression_stat,
        rpcStatsContext.replySizeBeforeCompression);
    proxy().stats().increment(
        reply_traffic_after_compression_stat,
        rpcStatsContext.replySizeAfterCompression);
  }

  proxy().stats().increment(destination_reqs_total_sum_stat);
  if (isRequestBufferDirty) {
    proxy().stats().increment(destination_reqs_dirty_buffer_sum_stat);
  }

  handleRxmittingConnection(result, latency);
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
    ProxyBase& proxy,
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath,
    folly::StringPiece routerInfoName) {
  std::shared_ptr<ProxyDestination> ptr(new ProxyDestination(
      proxy, std::move(ap), timeout, qosClass, qosPath, routerInfoName));
  ptr->selfPtr_ = ptr;
  return ptr;
}

ProxyDestination::~ProxyDestination() {
  if (proxy().destinationMap()) {
    // Only remove if we are not shutting down Proxy.
    proxy().destinationMap()->removeDestination(*this);
  }

  if (client_) {
    client_->setStatusCallbacks(nullptr, nullptr);
    client_->closeNow();
  }

  onTransitionFromState(stats().state);
  proxy().stats().decrement(num_servers_stat);
  if (accessPoint()->useSsl()) {
    proxy().stats().decrement(num_ssl_servers_stat);
  }
}

ProxyDestination::ProxyDestination(
    ProxyBase& proxy,
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath,
    folly::StringPiece routerInfoName)
    : ProxyDestinationBase(
          proxy,
          std::move(ap),
          timeout,
          qosClass,
          qosPath,
          routerInfoName),
      rxmitsToCloseConnection_(
          proxy.router().opts().min_rxmit_reconnect_threshold) {}

void ProxyDestination::resetInactive() {
  // No need to reset non-existing client.
  if (client_) {
    std::unique_ptr<AsyncMcClient> client;
    {
      folly::SpinLockGuard g(clientLock_);
      client = std::move(client_);
    }
    client->closeNow();
    stats().inactiveConnectionClosedTimestampUs = nowUs();
  }
}

void ProxyDestination::setPoolStatsIndex(int32_t index) {
  proxy().eventBase().runInEventBaseThread([selfPtr = selfPtr_, index]() {
    auto pdstn = selfPtr.lock();
    if (!pdstn) {
      return;
    }
    pdstn->stats().poolStatIndex_ = index;
    if (pdstn->stats().state == State::Up) {
      if (auto* poolStats = pdstn->proxy().stats().getPoolStats(index)) {
        poolStats->updateConnections(1);
      }
    }
  });
}

void ProxyDestination::updatePoolStatConnections(bool connected) {
  if (auto poolStats = proxy().stats().getPoolStats(stats().poolStatIndex_)) {
    poolStats->updateConnections(connected ? 1 : -1);
  }
}

void ProxyDestination::initializeAsyncMcClient() {
  assert(!client_);

  ConnectionOptions options(accessPoint());
  auto& opts = proxy().router().opts();
  options.tcpKeepAliveCount = opts.keepalive_cnt;
  options.tcpKeepAliveIdle = opts.keepalive_idle_s;
  options.tcpKeepAliveInterval = opts.keepalive_interval_s;
  options.numConnectTimeoutRetries = opts.connect_timeout_retries;
  options.connectTimeout = shortestConnectTimeout();
  options.writeTimeout = shortestWriteTimeout();
  options.routerInfoName = routerInfoName();
  options.payloadFormat = opts.use_compact_serialization
      ? PayloadFormat::CompactProtocolCompatibility
      : PayloadFormat::Carbon;
  if (!opts.debug_fifo_root.empty()) {
    options.debugFifoPath = getClientDebugFifoFullPath(opts);
  }
  if (opts.enable_qos) {
    options.enableQoS = true;
    options.qosClass = qosClass();
    options.qosPath = qosPath();
  }
  options.useJemallocNodumpAllocator = opts.jemalloc_nodump_buffers;
  if (accessPoint()->compressed()) {
    if (auto codecManager = proxy().router().getCodecManager()) {
      options.compressionCodecMap = codecManager->getCodecMap();
    }
  }

  if (accessPoint()->useSsl()) {
    options.securityOpts.sslPemCertPath = opts.pem_cert_path;
    options.securityOpts.sslPemKeyPath = opts.pem_key_path;
    if (opts.ssl_verify_peers) {
      options.securityOpts.sslPemCaPath = opts.pem_ca_path;
    }
    options.securityOpts.sessionCachingEnabled = opts.ssl_connection_cache;
    options.securityOpts.sslHandshakeOffload = opts.ssl_handshake_offload;
    options.securityOpts.sslServiceIdentity = opts.ssl_service_identity;
    options.securityOpts.tfoEnabledForSsl = opts.enable_ssl_tfo;
  }

  auto client =
      std::make_unique<AsyncMcClient>(proxy().eventBase(), std::move(options));
  {
    folly::SpinLockGuard g(clientLock_);
    client_ = std::move(client);
  }

  client_->setFlushList(&proxy().flushList());

  client_->setRequestStatusCallbacks(
      [this](int pending, int inflight) { // onStateChange
        if (pending != 0) {
          proxy().stats().increment(destination_pending_reqs_stat, pending);
          proxy().stats().setValue(
              destination_max_pending_reqs_stat,
              std::max(
                  proxy().stats().getValue(destination_max_pending_reqs_stat),
                  proxy().stats().getValue(destination_pending_reqs_stat)));
        }
        if (inflight != 0) {
          proxy().stats().increment(destination_inflight_reqs_stat, inflight);
          proxy().stats().setValue(
              destination_max_inflight_reqs_stat,
              std::max(
                  proxy().stats().getValue(destination_max_inflight_reqs_stat),
                  proxy().stats().getValue(destination_inflight_reqs_stat)));
        }
      },
      [this](size_t numToSend) { // onWrite
        proxy().stats().increment(num_socket_writes_stat);
        proxy().stats().increment(destination_batches_sum_stat);
        proxy().stats().increment(destination_requests_sum_stat, numToSend);
      },
      [this]() { // onPartialWrite
        proxy().stats().increment(num_socket_partial_writes_stat);
      });

  client_->setStatusCallbacks(
      [this](
          const folly::AsyncTransportWrapper& socket,
          int64_t numConnectRetries) mutable {
        setState(State::Up);
        proxy().stats().increment(num_connections_opened_stat);

        updatePoolStatConnections(true);

        if (const auto* sslSocket =
                socket.getUnderlyingTransport<folly::AsyncSSLSocket>()) {
          proxy().stats().increment(num_ssl_connections_opened_stat);
          if (sslSocket->sessionResumptionAttempted()) {
            proxy().stats().increment(num_ssl_resumption_attempts_stat);
          }
          if (sslSocket->getSSLSessionReused()) {
            proxy().stats().increment(num_ssl_resumption_successes_stat);
          }
        } else if (
            const auto* fizzSock =
                socket.getUnderlyingTransport<McFizzClient>()) {
          proxy().stats().increment(num_ssl_connections_opened_stat);
          if (fizzSock->pskResumed()) {
            proxy().stats().increment(num_ssl_resumption_successes_stat);
            proxy().stats().increment(num_ssl_resumption_attempts_stat);
          } else {
            auto pskState = fizzSock->getState().pskType();
            if (pskState && pskState.value() == fizz::PskType::Rejected) {
              // session resumption was attempted, but failed
              proxy().stats().increment(num_ssl_resumption_attempts_stat);
            }
          }
        }

        if (numConnectRetries > 0) {
          proxy().stats().increment(num_connect_success_after_retrying_stat);
          proxy().stats().increment(
              num_connect_retries_stat, numConnectRetries);
        }

        updateConnectionClosedInternalStat();
      },
      [pdstnPtr = selfPtr_](
          AsyncMcClient::ConnectionDownReason reason,
          int64_t numConnectRetries) {
        auto pdstn = pdstnPtr.lock();
        if (!pdstn) {
          LOG(WARNING) << "Proxy destination is already destroyed. "
                          "Stats will not be bumped.";
          return;
        }

        pdstn->proxy().stats().increment(num_connections_closed_stat);
        if (pdstn->accessPoint()->useSsl()) {
          pdstn->proxy().stats().increment(num_ssl_connections_closed_stat);
        }

        pdstn->updatePoolStatConnections(false);

        if (reason == AsyncMcClient::ConnectionDownReason::ABORTED) {
          pdstn->setState(State::Closed);
        } else {
          // In case of server going away, we should gracefully close the
          // connection (i.e. allow remaining outstanding requests to drain).
          if (reason == AsyncMcClient::ConnectionDownReason::SERVER_GONE_AWAY) {
            pdstn->closeGracefully();
          }
          pdstn->setState(State::Down);
          pdstn->handleTko(
              reason == AsyncMcClient::ConnectionDownReason::CONNECT_TIMEOUT
                  ? carbon::Result::CONNECT_TIMEOUT
                  : carbon::Result::CONNECT_ERROR,
              /* is_probe_req= */ false);
        }

        pdstn->proxy().stats().increment(
            num_connect_retries_stat, numConnectRetries);
      });

  if (opts.target_max_inflight_requests > 0) {
    client_->setThrottle(
        opts.target_max_inflight_requests, opts.target_max_pending_requests);
  }
}

void ProxyDestination::updateConnectionClosedInternalStat() {
  if (stats().inactiveConnectionClosedTimestampUs != 0) {
    std::chrono::microseconds timeClosed(
        nowUs() - stats().inactiveConnectionClosedTimestampUs);
    proxy().stats().inactiveConnectionClosedIntervalSec().insertSample(
        std::chrono::duration_cast<std::chrono::seconds>(timeClosed).count());

    // resets inactiveConnectionClosedTimestampUs, as we just want to take
    // into account connections that were closed due to lack of activity
    stats().inactiveConnectionClosedTimestampUs = 0;
  }
}

void ProxyDestination::closeGracefully() {
  if (client_) {
    // In case we have outstanding probe, we should close now, to get it
    // properly cleared.
    if (probeInflight()) {
      client_->closeNow();
    }
    // Check again, in case we reset it in closeNow()
    if (client_) {
      client_->setStatusCallbacks(nullptr, nullptr);
      std::unique_ptr<AsyncMcClient> client;
      {
        folly::SpinLockGuard g(clientLock_);
        client = std::move(client_);
      }
      client.reset();
    }
  }
}

AsyncMcClient& ProxyDestination::getAsyncMcClient() {
  if (!client_) {
    initializeAsyncMcClient();
  }
  return *client_;
}


void ProxyDestination::setState(State newState) {
  if (stats().state == newState) {
    return;
  }

  auto logUtil = [this](const char* s) {
    VLOG(3) << "server " << pdstnKey_ << " " << s << " ("
            << proxy().stats().getValue(num_servers_up_stat) << " of "
            << proxy().stats().getValue(num_servers_stat) << ")";
  };

  onTransitionFromState(stats().state);
  onTransitionToState(newState);
  stats().state = newState;

  switch (stats().state) {
    case State::Up:
      logUtil("up");
      break;
    case State::Closed:
      logUtil("closed");
      break;
    case State::Down:
      logUtil("down");
      break;
    case State::New:
    case State::NumStates:
      assert(false);
      break;
  }
}

void ProxyDestination::onTransitionToState(State st) {
  onTransitionImpl(st, true /* to */);
}

void ProxyDestination::onTransitionFromState(State st) {
  onTransitionImpl(st, false /* to */);
}

void ProxyDestination::onTransitionImpl(State st, bool to) {
  const int64_t delta = to ? 1 : -1;
  const bool ssl = accessPoint()->useSsl();

  switch (st) {
    case ProxyDestination::State::New: {
      proxy().stats().increment(num_servers_new_stat, delta);
      if (ssl) {
        proxy().stats().increment(num_ssl_servers_new_stat, delta);
      }
      break;
    }
    case ProxyDestination::State::Up: {
      proxy().stats().increment(num_servers_up_stat, delta);
      if (ssl) {
        proxy().stats().increment(num_ssl_servers_up_stat, delta);
      }
      break;
    }
    case ProxyDestination::State::Closed: {
      proxy().stats().increment(num_servers_closed_stat, delta);
      if (ssl) {
        proxy().stats().increment(num_ssl_servers_closed_stat, delta);
      }
      break;
    }
    case ProxyDestination::State::Down: {
      proxy().stats().increment(num_servers_down_stat, delta);
      if (ssl) {
        proxy().stats().increment(num_ssl_servers_down_stat, delta);
      }
      break;
    }
    case ProxyDestination::State::NumStates: {
      CHECK(false);
      break;
    }
  }
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
