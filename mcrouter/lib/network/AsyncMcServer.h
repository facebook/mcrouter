/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <sys/socket.h>

#include <folly/SharedMutex.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/CongestionController.h"

namespace folly {
class EventBase;
class ScopedEventBaseThread;
} // folly

namespace wangle {
class TLSCredProcessor;
} // wangle

namespace facebook {
namespace memcache {

class AsyncMcServerWorker;
class McServerThread;
class McServerThreadSpawnController;

/**
 * A multithreaded, asynchronous MC protocol server.
 */
class AsyncMcServer {
 public:
  /**
   * Server startup options
   */
  struct Options {
    /**
     * Take over an exernally created socket.
     * The server will call listen(), but not bind().
     * If this is used (not -1), ports must be empty.
     * It will be used as SSL socket if and only if all of pem* paths are set.
     */
    int existingSocketFd{-1};

    /**
     * Create Unix Domain Socket to listen on.
     * If this is used (not empty), port must be empty,
     * existingSocketFd must be unset (-1).
     */
    std::string unixDomainSockPath;

    /**
     * TCP listen backlog
     */
    int tcpListenBacklog{SOMAXCONN};

    /**
     * The list of ports to listen on.
     * If this is used, existingSocketFd must be unset (-1).
     */
    std::vector<uint16_t> ports;

    /**
     * The list of ports to listen on for SSL connections.
     * If this is used, existingSocketFd must be unset (-1).
     */
    std::vector<uint16_t> sslPorts;

    /**
     * SSL cert/key/CA paths.
     * If sslPorts is non-empty, these must also be nonempty.
     */
    std::string pemCertPath;
    std::string pemKeyPath;
    std::string pemCaPath;

    /**
     * Path to JSON file containing old, current, and new seeds used for TLS
     * ticket key generation.
     */
    std::string tlsTicketKeySeedPath;

    /**
     * TFO settings (for SSL only)
     */
    bool tfoEnabledForSsl{false};
    uint32_t tfoQueueSize{0};

    /**
     * Number of threads to spawn, must be positive.
     */
    size_t numThreads{1};

    /**
     * Worker-specific options
     */
    AsyncMcServerWorkerOptions worker;

    /**
     * CPU-based congestion controller.
     */
    CongestionControllerOptions cpuControllerOpts;

    /**
     * Memory-based congestion controller.
     */
    CongestionControllerOptions memoryControllerOpts;

    /**
     * @param globalMaxConns
     *  0  - do not reap connections;
     *  1  - calculate maximum based on rlimits;
     *  >1 - set per worker limits to ceil(globalMaxConns / numThreads)
     * @param numThreads_  usually the same as `numThreads` field.
     */
    void setPerThreadMaxConns(size_t globalMaxConns, size_t numThreads_);
  };

  /**
   * User-defined loop function.
   * Args are threadId (0 to numThreads - 1), eventBase and the thread's worker
   * The user is responsible for calling eventBase.loop() or similar.
   */
  typedef std::function<
      void(size_t, folly::EventBase&, facebook::memcache::AsyncMcServerWorker&)>
      LoopFn;

  explicit AsyncMcServer(Options opts);
  ~AsyncMcServer();

  /**
   * @return Event bases from all worker threads ordered by threadId.
   */
  std::vector<folly::EventBase*> eventBases() const;

  /**
   * Spawn the required number of threads, and run the loop function in each
   * of them.
   *
   * @param onShutdown  called on shutdown. Worker threads will be stopped only
   *                    after the callback completes. It may be called from any
   *                    thread, but it is guaranteed the callback will be
   *                    executed exactly one time.
   *
   * @throws folly::AsyncSocketException
   *   If bind or listen fails.
   */
  void spawn(LoopFn fn, std::function<void()> onShutdown = nullptr);

  /**
   * Start shutting down all processing gracefully.  Will ensure that any
   * pending requests are replied, and any writes on the sockets complete.
   * Can only be called after spawn();
   *
   * Note: you still have to call join() to wait for all writes to complete.
   *
   * Can be called from any thread. Safe to call multiple times - the first
   * call will start shutting down, subsequent calls will not have any effect.
   */
  void shutdown();

  /**
   * Installs a new handler for the given signals that would shutdown
   * this server when delivered.
   * Note: only one server can be managed by these handlers per process.
   *
   * @param signals  List of signals
   */
  void installShutdownHandler(const std::vector<int>& signals);

  /**
   * Signal handler-safe version of shutdown.
   * Can only be called after spawn().
   */
  void shutdownFromSignalHandler();

  /**
   * Join all spawned threads.  Will exit upon server shutdown.
   */
  void join();

  /**
   * Getter/setter for seeds to be used to generate keys encrypting TLS tickets.
   */
  void setTicketKeySeeds(wangle::TLSTicketKeySeeds seeds);
  wangle::TLSTicketKeySeeds getTicketKeySeeds() const;

 private:
  std::unique_ptr<folly::ScopedEventBaseThread> auxiliaryEvbThread_;
  Options opts_;
  std::unique_ptr<McServerThreadSpawnController> threadsSpawnController_;
  std::vector<std::unique_ptr<McServerThread>> threads_;

  std::unique_ptr<wangle::TLSCredProcessor> ticketKeySeedPoller_;
  wangle::TLSTicketKeySeeds tlsTicketKeySeeds_;
  mutable folly::SharedMutex tlsTicketKeySeedsLock_;

  std::function<void()> onShutdown_;
  std::atomic<bool> alive_{true};
  bool spawned_{false};

  enum class SignalShutdownState : uint64_t { STARTUP, SHUTDOWN, SPAWNED };
  std::atomic<SignalShutdownState> signalShutdownState_{
      SignalShutdownState::STARTUP};

  void startPollingTicketKeySeeds();

  AsyncMcServer(const AsyncMcServer&) = delete;
  AsyncMcServer& operator=(const AsyncMcServer&) = delete;

  friend class McServerThread;
};

} // memcache
} // facebook
