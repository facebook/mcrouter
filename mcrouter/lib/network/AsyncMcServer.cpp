/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <mutex>
#include <thread>
#include <vector>

#include <folly/Memory.h>
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
} // anonymous

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

  void handlerReady(uint16_t events) noexcept override final {
    LOG(INFO) << "Shutting down on signal";
    server_.shutdown();
  }
};

class McServerThread {
 public:
  explicit McServerThread(AsyncMcServer& server)
      : server_(server),
        evb_(folly::make_unique<folly::EventBase>(
            /* enableTimeMeasurement */ false)),
        worker_(server.opts_.worker, *evb_),
        acceptCallback_(this, false),
        sslAcceptCallback_(this, true) {}

  enum AcceptorT { Acceptor };

  McServerThread(AcceptorT, AsyncMcServer& server)
      : server_(server),
        evb_(folly::make_unique<folly::EventBase>(
            /* enableTimeMeasurement */ false)),
        worker_(server.opts_.worker, *evb_),
        acceptCallback_(this, false),
        sslAcceptCallback_(this, true),
        accepting_(true),
        shutdownPipe_(folly::make_unique<ShutdownPipe>(server, *evb_)) {}

  folly::EventBase& eventBase() {
    return *evb_;
  }

  void waitForAcceptor() {
    std::unique_lock<std::mutex> lock(acceptorLock_);
    acceptorCv_.wait(lock, [this]() { return acceptorSetup_; });
    if (spawnException_) {
      thread_.join();
      std::rethrow_exception(spawnException_);
    }
  }

  void spawn(AsyncMcServer::LoopFn fn, size_t threadId) {
    worker_.setOnShutdownOperation([&]() { server_.shutdown(); });

    thread_ = std::thread{[fn, threadId, this]() {
      if (accepting_) {
        startAccepting();

        if (spawnException_) {
          return;
        }
      }

      fn(threadId, *evb_, worker_);

      // Detach the server sockets from the acceptor thread.
      // If we don't do this, the TAsyncSSLServerSocket destructor
      // will try to do it, and a segfault will result if the
      // socket destructor runs after the threads' destructors.
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

      evb_.reset();
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

 private:
  class AcceptCallback : public folly::AsyncServerSocket::AcceptCallback {
   public:
    AcceptCallback(McServerThread* mcServerThread, bool secure)
        : mcServerThread_(mcServerThread), secure_(secure) {}
    void connectionAccepted(
        int fd,
        const folly::SocketAddress& clientAddr) noexcept override final {
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
    void acceptError(const std::exception& ex) noexcept override final {
      LOG(ERROR) << "Connection accept error: " << ex.what();
    }

   private:
    McServerThread* mcServerThread_{nullptr};
    bool secure_{false};
  };

  AsyncMcServer& server_;
  std::unique_ptr<folly::EventBase> evb_;
  std::thread thread_;
  AsyncMcServerWorker worker_;
  AcceptCallback acceptCallback_;
  AcceptCallback sslAcceptCallback_;

  bool accepting_{false};

  std::mutex acceptorLock_;
  std::condition_variable acceptorCv_;
  /** True only after acceptor callbacks are setup */
  bool acceptorSetup_{false};
  std::exception_ptr spawnException_;

  folly::AsyncServerSocket::UniquePtr socket_;
  folly::AsyncServerSocket::UniquePtr sslSocket_;
  std::vector<std::pair<folly::EventBase*, folly::Executor::KeepAlive>>
      acceptorsKeepAlive_;
  std::unique_ptr<ShutdownPipe> shutdownPipe_;

  void startAccepting() {
    CHECK(accepting_);
    try {
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
    } catch (...) {
      spawnException_ = std::current_exception();
    }

    {
      std::lock_guard<std::mutex> lock(acceptorLock_);
      acceptorSetup_ = true;
    }
    acceptorCv_.notify_all();
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
  if (opts_.congestionController.cpuControlTarget > 0 ||
      opts_.congestionController.memControlTarget > 0) {
    auxiliaryEvbThread_ = folly::make_unique<folly::ScopedEventBaseThread>();

    if (opts_.congestionController.cpuControlTarget > 0) {
      opts_.worker.cpuController = std::make_shared<CpuController>(
          opts_.congestionController.cpuControlTarget,
          *auxiliaryEvbThread_->getEventBase(),
          opts_.congestionController.cpuControlDelay);
      opts_.worker.cpuController->start();
    }

    if (opts_.congestionController.memControlTarget > 0) {
      opts_.worker.memController = std::make_shared<MemoryController>(
          opts_.congestionController.memControlTarget,
          *auxiliaryEvbThread_->getEventBase(),
          opts_.congestionController.memControlDelay);
      opts_.worker.memController->start();
    }
  }

  if (!opts_.tlsTicketKeySeedPath.empty()) {
    if (auto initialSeeds = wangle::TLSCredProcessor::processTLSTickets(
            opts_.tlsTicketKeySeedPath)) {
      tlsTicketKeySeeds_ = std::move(*initialSeeds);
    }
    startPollingTicketKeySeeds();
  }

  if (opts_.numThreads == 0) {
    throw std::invalid_argument(folly::sformat(
        "Unexpected option: opts_.numThreads={}", opts_.numThreads));
  }
  threads_.emplace_back(
      folly::make_unique<McServerThread>(McServerThread::Acceptor, *this));
  for (size_t i = 1; i < opts_.numThreads; ++i) {
    threads_.emplace_back(folly::make_unique<McServerThread>(*this));
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

  /* We need to make sure we register all acceptor callbacks before
     running spawn() on other threads. This is so that eventBase.loop()
     never exits immediately on non-acceptor threads. */
  threads_[0]->spawn(fn, 0);
  threads_[0]->waitForAcceptor();
  for (size_t id = 1; id < threads_.size(); ++id) {
    threads_[id]->spawn(fn, id);
  }

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
  ticketKeySeedPoller_ = folly::make_unique<wangle::TLSCredProcessor>(
      opts_.tlsTicketKeySeedPath, opts_.pemCertPath);
  ticketKeySeedPoller_->addTicketCallback(
      [this](wangle::TLSTicketKeySeeds updatedSeeds) {
        setTicketKeySeeds(std::move(updatedSeeds));
        VLOG(0) << "Updated TLSTicketKeySeeds";
      });
}

} // memcache
} // facebook
