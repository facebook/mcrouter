/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsyncMcServer.h"

#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <folly/Conv.h>
#include <folly/SharedMutex.h>
#include <folly/String.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <wangle/ssl/TLSCredProcessor.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"

namespace facebook {
namespace memcache {

namespace {

/* Global pointer to the server for signal handlers */
facebook::memcache::AsyncMcServer* gServer;

class ShutdownPipe : public folly::EventHandler {
 public:
  ShutdownPipe(AsyncMcServer& server, folly::EventBase& evb)
      : folly::EventHandler(&evb), server_(server) {
    fd_ = eventfd(0, 0);
    if (UNLIKELY(fd_ == -1)) {
      throw std::runtime_error(
          "Unexpected file descriptor (-1) in ShutdownPipe");
    }
    changeHandlerFD(fd_);
    registerHandler(EV_READ);
  }

  void shutdownFromSignalHandler() {
    uint64_t val = 1;
    auto res = write(fd_, &val, 8);
    if (UNLIKELY(res != 8)) {
      throw std::system_error(
          errno,
          std::system_category(),
          folly::sformat("Unexpected return of write: {}", res));
    }
  }

 private:
  AsyncMcServer& server_;
  int fd_;

  void handlerReady(uint16_t /* events */) noexcept final {
    LOG(INFO) << "Shutting down on signal";
    server_.shutdown();
  }
};

} // anonymous namespace

/**
 * Class responsible for orchestrating the startup of McServerThreads.
 */
class McServerThreadSpawnController {
 public:
  /**
   * Blocks the current thread until it's ready to start running.
   */
  void waitToStart() {
    runningFuture_.get();
  }

  /**
   * Blocks until the acceptor thread is ready to accept new connections.
   *
   * @throw  If anything was thrown when starting to accept connections.
   */
  void waitForAcceptor() {
    acceptorFuture_.get();
  }

  /**
   * Wake up all threads to start running.
   */
  void startRunning() {
    runningPromise_.set_value();
  }

  /**
   * Abort execution.
   * Used only when we want to shutdown the server before we started running.
   */
  void abort() {
    runningPromise_.set_exception(
        std::make_exception_ptr(std::runtime_error("Execution aborted.")));
  }

  /**
   * Starts accepting (if it's an acceptor thread), or blocks until accetor
   * thread has started accepting.
   *
   * @param acceptorFn        The acceptor function. This function should throw
   *                          if something goes wrong.
   * @param isAcceptorThread  Whether or not this is the acceptor thread.
   *
   * @throw  If anything was thrown when starting to accept connections.
   */
  template <class F>
  void startAccepting(F&& acceptorFn, bool isAcceptorThread) {
    if (isAcceptorThread) {
      try {
        acceptorFn();
        acceptorPromise_.set_value();
      } catch (...) {
        auto exception = std::current_exception();
        acceptorPromise_.set_exception(exception);
        std::rethrow_exception(exception);
      }
    } else {
      waitForAcceptor();
    }
  }

 private:
  std::promise<void> runningPromise_;
  std::shared_future<void> runningFuture_{runningPromise_.get_future()};

  std::promise<void> acceptorPromise_;
  std::shared_future<void> acceptorFuture_{acceptorPromise_.get_future()};
};

class McServerThread {
 public:
  explicit McServerThread(AsyncMcServer& server, size_t id)
      : server_(server),
        evb_(std::make_unique<folly::EventBase>(
            server.opts_.worker.enableEventBaseTimeMeasurement)),
        id_(id),
        worker_(server.opts_.worker, *evb_),
        acceptCallback_(this, false),
        sslAcceptCallback_(this, true),
        accepting_(false) {
    startThread();
  }

  enum AcceptorT { Acceptor };

  McServerThread(AcceptorT, AsyncMcServer& server, size_t id)
      : server_(server),
        evb_(std::make_unique<folly::EventBase>(
            server.opts_.worker.enableEventBaseTimeMeasurement)),
        id_(id),
        worker_(server.opts_.worker, *evb_),
        acceptCallback_(this, false),
        sslAcceptCallback_(this, true),
        accepting_(true),
        shutdownPipe_(std::make_unique<ShutdownPipe>(server, *evb_)) {
    startThread();
  }

  folly::EventBase& eventBase() {
    return *evb_;
  }

  void startThread() {
    worker_.setOnShutdownOperation([&]() { server_.shutdown(); });

    thread_ = std::thread{[this]() {
      SCOPE_EXIT {
        // We must detroy the EventBase in it's own thread.
        // The reason is that we might have already scheduled something
        // using some VirtualEventBase, and it won't allow the EventBase
        // to be destroyed until all keepAlive tokens have being released.
        // In this case evb_.reset() will loop the EventBase until we are done
        // releasing all keepAlive tokens, so we can safely destroy
        // VirtualEventBase from main thread.
        evb_.reset();
      };

      try {
        server_.threadsSpawnController_->waitToStart();

        server_.threadsSpawnController_->startAccepting(
            [this]() { startAccepting(); }, accepting_);
      } catch (...) {
        // if an exception is thrown, something went wrong before startup.
        return;
      }

      fn_(id_, *evb_, worker_);

      // Detach the server sockets from the acceptor thread.
      // If we don't do this, the TAsyncSSLServerSocket destructor
      // will try to do it, and a segfault will result if the
      // socket destructor runs after the threads' destructors.
      if (accepting_) {
        socket_.reset();
        sslSocket_.reset();
        for (auto& acceptor : acceptorsKeepAlive_) {
          acceptor.first->add(
              [keepAlive = std::move(acceptor.second)]() mutable {
                keepAlive.reset();
              });
        }
        acceptorsKeepAlive_.clear();
      }
    }};
  }

  /* Safe to call from other threads */
  void shutdown() {
    auto result = evb_->runInEventBaseThread([&]() {
      if (accepting_) {
        socket_.reset();
        sslSocket_.reset();
        for (auto& acceptor : acceptorsKeepAlive_) {
          acceptor.first
              ->add([keepAlive = std::move(acceptor.second)]() mutable {
                keepAlive.reset();
              });
        }
        acceptorsKeepAlive_.clear();
      }
      if (shutdownPipe_) {
        shutdownPipe_->unregisterHandler();
      }
      worker_.shutdown();
    });

    if (!result) {
      throw std::runtime_error("error calling runInEventBaseThread");
    }
  }

  void shutdownFromSignalHandler() {
    if (shutdownPipe_) {
      shutdownPipe_->shutdownFromSignalHandler();
    }
  }

  void join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void setLoopFn(AsyncMcServer::LoopFn fn) {
    fn_ = std::move(fn);
  }

 private:
  class AcceptCallback : public folly::AsyncServerSocket::AcceptCallback {
   public:
    AcceptCallback(McServerThread* mcServerThread, bool secure)
        : mcServerThread_(mcServerThread), secure_(secure) {}
    void connectionAccepted(
        int fd,
        const folly::SocketAddress& /* clientAddr */) noexcept final {
      if (secure_) {
        const auto& server = mcServerThread_->server_;
        auto& opts = server.opts_;
        auto sslCtx = getSSLContext(
            opts.pemCertPath,
            opts.pemKeyPath,
            opts.pemCaPath,
            server.getTicketKeySeeds());

        if (sslCtx) {
          sslCtx->setVerificationOption(
              folly::SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
          mcServerThread_->worker_.addSecureClientSocket(fd, std::move(sslCtx));
        } else {
          ::close(fd);
        }
      } else {
        mcServerThread_->worker_.addClientSocket(fd);
      }
    }
    void acceptError(const std::exception& ex) noexcept final {
      LOG(ERROR) << "Connection accept error: " << ex.what();
    }

   private:
    McServerThread* mcServerThread_{nullptr};
    bool secure_{false};
  };

  AsyncMcServer& server_;
  std::unique_ptr<folly::EventBase> evb_;
  size_t id_;
  AsyncMcServerWorker worker_;
  AcceptCallback acceptCallback_;
  AcceptCallback sslAcceptCallback_;
  bool accepting_{false};
  std::thread thread_;

  folly::AsyncServerSocket::UniquePtr socket_;
  folly::AsyncServerSocket::UniquePtr sslSocket_;
  std::vector<std::pair<folly::EventBase*, folly::Executor::KeepAlive>>
      acceptorsKeepAlive_;
  std::unique_ptr<ShutdownPipe> shutdownPipe_;

  AsyncMcServer::LoopFn fn_;

  /**
   * Start accepting new connections.
   *
   * @throw   If anything goes wrong when start accepting connections.
   */
  void startAccepting() {
    CHECK(accepting_);
    auto& opts = server_.opts_;

    if (opts.existingSocketFd != -1) {
      checkLogic(
          opts.ports.empty() && opts.sslPorts.empty(),
          "Can't use ports if using existing socket");
      if (!opts.pemCertPath.empty() || !opts.pemKeyPath.empty() ||
          !opts.pemCaPath.empty()) {
        checkLogic(
            !opts.pemCertPath.empty() && !opts.pemKeyPath.empty() &&
                !opts.pemCaPath.empty(),
            "All of pemCertPath, pemKeyPath and pemCaPath are required "
            "if at least one of them set");

        sslSocket_.reset(new folly::AsyncServerSocket());
        sslSocket_->useExistingSocket(opts.existingSocketFd);
      } else {
        socket_.reset(new folly::AsyncServerSocket());
        socket_->useExistingSocket(opts.existingSocketFd);
      }
    } else if (!opts.unixDomainSockPath.empty()) {
      checkLogic(
          opts.ports.empty() && opts.sslPorts.empty() &&
              (opts.existingSocketFd == -1),
          "Can't listen on port and unix domain socket at the same time");
      std::remove(opts.unixDomainSockPath.c_str());
      socket_.reset(new folly::AsyncServerSocket());
      folly::SocketAddress serverAddress;
      serverAddress.setFromPath(opts.unixDomainSockPath);
      socket_->bind(serverAddress);
    } else {
      checkLogic(
          !server_.opts_.ports.empty() || !server_.opts_.sslPorts.empty(),
          "At least one port (plain or SSL) must be speicified");
      if (!server_.opts_.ports.empty()) {
        socket_.reset(new folly::AsyncServerSocket());
        for (auto port : server_.opts_.ports) {
          socket_->bind(port);
        }
      }
      if (!server_.opts_.sslPorts.empty()) {
        checkLogic(
            !server_.opts_.pemCertPath.empty() &&
                !server_.opts_.pemKeyPath.empty() &&
                !server_.opts_.pemCaPath.empty(),
            "All of pemCertPath, pemKeyPath, pemCaPath required"
            " with sslPorts");

        sslSocket_.reset(new folly::AsyncServerSocket());
        for (auto sslPort : server_.opts_.sslPorts) {
          sslSocket_->bind(sslPort);
        }
      }
    }

    if (socket_) {
      socket_->listen(server_.opts_.tcpListenBacklog);
      socket_->startAccepting();
      socket_->attachEventBase(evb_.get());
    }
    if (sslSocket_) {
      if (server_.opts_.tfoEnabledForSsl) {
        sslSocket_->setTFOEnabled(true, server_.opts_.tfoQueueSize);
      } else {
        sslSocket_->setTFOEnabled(false, 0);
      }
      sslSocket_->listen(server_.opts_.tcpListenBacklog);
      sslSocket_->startAccepting();
      sslSocket_->attachEventBase(evb_.get());
    }

    for (auto& t : server_.threads_) {
      if (socket_ != nullptr) {
        socket_->addAcceptCallback(&t->acceptCallback_, t->evb_.get());
      }
      if (sslSocket_ != nullptr) {
        sslSocket_->addAcceptCallback(&t->sslAcceptCallback_, t->evb_.get());
      }
      if (socket_ != nullptr || sslSocket_ != nullptr || t.get() != this) {
        acceptorsKeepAlive_.emplace_back(
            t->evb_.get(), t->evb_->getKeepAliveToken());
      }
    }
  }
};

void AsyncMcServer::Options::setPerThreadMaxConns(
    size_t globalMaxConns,
    size_t numThreads_) {
  if (globalMaxConns == 0) {
    worker.maxConns = 0;
    return;
  }
  assert(numThreads_ > 0);
  if (globalMaxConns == 1) {
    rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
      LOG(ERROR) << "getrlimit failed. Errno: " << folly::errnoStr(errno)
                 << ". Disabling connection limits.";
      worker.maxConns = 0;
      return;
    }

    size_t softLimit = rlim.rlim_cur;
    globalMaxConns = std::max<size_t>(softLimit, 3) - 3;
    VLOG(1) << "Setting max conns to " << globalMaxConns
            << " based on soft resource limit of " << softLimit;
  }
  worker.maxConns = (globalMaxConns + numThreads_ - 1) / numThreads_;
}

AsyncMcServer::AsyncMcServer(Options opts) : opts_(std::move(opts)) {
  if (opts_.cpuControllerOpts.shouldEnable() ||
      opts_.memoryControllerOpts.shouldEnable()) {
    auxiliaryEvbThread_ = std::make_unique<folly::ScopedEventBaseThread>();

    if (opts_.cpuControllerOpts.shouldEnable()) {
      opts_.worker.cpuController = std::make_shared<CpuController>(
          opts_.cpuControllerOpts, *auxiliaryEvbThread_->getEventBase());
      opts_.worker.cpuController->start();
    }

    if (opts_.memoryControllerOpts.shouldEnable()) {
      opts_.worker.memController = std::make_shared<MemoryController>(
          opts_.memoryControllerOpts, *auxiliaryEvbThread_->getEventBase());
      opts_.worker.memController->start();
    }
  }

  if (!opts_.tlsTicketKeySeedPath.empty()) {
    if (auto initialSeeds = wangle::TLSCredProcessor::processTLSTickets(
            opts_.tlsTicketKeySeedPath)) {
      tlsTicketKeySeeds_ = std::move(*initialSeeds);
      VLOG(0) << folly::to<std::string>(
          "Successfully loaded ticket key seeds from ",
          opts_.tlsTicketKeySeedPath);
    } else {
      LOG(ERROR) << folly::to<std::string>(
          "Unable to load ticket key seeds from ", opts_.tlsTicketKeySeedPath);
    }
    startPollingTicketKeySeeds();
  }

  if (opts_.numThreads == 0) {
    throw std::invalid_argument(folly::sformat(
        "Unexpected option: opts_.numThreads={}", opts_.numThreads));
  }
  threadsSpawnController_ = std::make_unique<McServerThreadSpawnController>();
  threads_.emplace_back(std::make_unique<McServerThread>(
      McServerThread::Acceptor, *this, /*id*/ 0));
  for (size_t id = 1; id < opts_.numThreads; ++id) {
    threads_.emplace_back(std::make_unique<McServerThread>(*this, id));
  }
}

std::vector<folly::EventBase*> AsyncMcServer::eventBases() const {
  std::vector<folly::EventBase*> out;
  for (auto& t : threads_) {
    out.push_back(&t->eventBase());
  }
  return out;
}

AsyncMcServer::~AsyncMcServer() {
  /* Need to place the destructor here, since this is the only
     translation unit that knows about McServerThread */
  if (opts_.worker.cpuController) {
    opts_.worker.cpuController->stop();
  }
  if (opts_.worker.memController) {
    opts_.worker.memController->stop();
  }

  /* In case some signal handlers are still registered */
  gServer = nullptr;
}

void AsyncMcServer::spawn(LoopFn fn, std::function<void()> onShutdown) {
  CHECK(threads_.size() == opts_.numThreads);

  onShutdown_ = std::move(onShutdown);

  for (size_t i = 0; i < threads_.size(); ++i) {
    threads_[i]->setLoopFn(fn);
  }

  threadsSpawnController_->startRunning();
  spawned_ = true;

  // this call will throw if something went wrong with acceptor.
  threadsSpawnController_->waitForAcceptor();

  /* We atomically attempt to change the state STARTUP -> SPAWNED.
     If we see the state SHUTDOWN, it means a signal handler ran
     concurrently with us (maybe even on this thread),
     so we just shutdown threads. */
  auto state = signalShutdownState_.load();
  do {
    if (state == SignalShutdownState::SHUTDOWN) {
      shutdown();
      return;
    }
  } while (!signalShutdownState_.compare_exchange_weak(
      state, SignalShutdownState::SPAWNED));
}

void AsyncMcServer::shutdown() {
  if (!alive_.exchange(false)) {
    return;
  }

  if (onShutdown_) {
    onShutdown_();
  }

  for (auto& thread : threads_) {
    thread->shutdown();
  }
}

void AsyncMcServer::installShutdownHandler(const std::vector<int>& signals) {
  gServer = this;

  /* prevent the above write being reordered with installing the handler */
  std::atomic_signal_fence(std::memory_order_seq_cst);

  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = [](int) {
    if (gServer) {
      gServer->shutdownFromSignalHandler();
    }
  };
  act.sa_flags = SA_RESETHAND;

  for (auto sig : signals) {
    if (sigaction(sig, &act, nullptr) == -1) {
      throw std::system_error(
          errno,
          std::system_category(),
          "Unexpected error returned by sigaction");
    }
  }
}

void AsyncMcServer::shutdownFromSignalHandler() {
  /* We atomically attempt to change the state STARTUP -> SHUTDOWN.
     If we see that the state is already SPAWNED,
     we can just issue a shutdown request. */

  auto state = signalShutdownState_.load();
  do {
    if (state == SignalShutdownState::SPAWNED) {
      threads_[0]->shutdownFromSignalHandler();
      return;
    }
  } while (!signalShutdownState_.compare_exchange_weak(
      state, SignalShutdownState::SHUTDOWN));
}

void AsyncMcServer::join() {
  if (!spawned_) {
    threadsSpawnController_->abort();
  }
  for (auto& thread : threads_) {
    thread->join();
  }
}

void AsyncMcServer::setTicketKeySeeds(wangle::TLSTicketKeySeeds seeds) {
  folly::SharedMutex::WriteHolder writeGuard(tlsTicketKeySeedsLock_);
  tlsTicketKeySeeds_ = std::move(seeds);
}

wangle::TLSTicketKeySeeds AsyncMcServer::getTicketKeySeeds() const {
  folly::SharedMutex::ReadHolder readGuard(tlsTicketKeySeedsLock_);
  return tlsTicketKeySeeds_;
}

void AsyncMcServer::startPollingTicketKeySeeds() {
  // Caller assumed to have checked opts_.tlsTicketKeySeedPath is non-empty
  ticketKeySeedPoller_ = std::make_unique<wangle::TLSCredProcessor>();
  ticketKeySeedPoller_->setTicketPathToWatch(opts_.tlsTicketKeySeedPath);
  ticketKeySeedPoller_->addTicketCallback(
      [this](wangle::TLSTicketKeySeeds updatedSeeds) {
        setTicketKeySeeds(std::move(updatedSeeds));
        VLOG(0) << "Updated TLSTicketKeySeeds";
      });
}

} // memcache
} // facebook
