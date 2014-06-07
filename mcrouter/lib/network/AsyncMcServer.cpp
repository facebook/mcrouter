#include "AsyncMcServer.h"

#include <signal.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "folly/io/async/EventBase.h"
#include "folly/Memory.h"
#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "thrift/lib/cpp/async/TAsyncServerSocket.h"
#include "thrift/lib/cpp/transport/TSSLSocket.h"

namespace facebook { namespace memcache {

using apache::thrift::transport::SSLContext;

namespace {
/* Global pointer to the server for signal handlers */
facebook::memcache::AsyncMcServer* gServer;
}


class ShutdownPipe : public folly::EventHandler {
public:
  ShutdownPipe(AsyncMcServer& server,
               folly::EventBase& evb)
      : folly::EventHandler(&evb),
        server_(server) {
    fd_ = eventfd(0, 0);
    CHECK(fd_ != -1);
    changeHandlerFD(fd_);
    registerHandler(EV_READ);
  }

  void shutdownFromSignalHandler() {
    uint64_t val = 1;
    write(fd_, &val, 8);
  }

 private:
  AsyncMcServer& server_;
  int fd_;

  void handlerReady(uint16_t events) noexcept override {
    LOG(INFO) << "Shutting down on signal";
    server_.shutdown();
  }
};

class McServerThread {
 public:
  explicit McServerThread(AsyncMcServer& server)
      : server_(server),
        worker_(server.opts_.worker, evb_),
        acceptCallback_(this, false),
        sslAcceptCallback_(this, true) {
  }

  enum AcceptorT { Acceptor };

  McServerThread(
    AcceptorT,
    AsyncMcServer& server)
      : server_(server),
        worker_(server.opts_.worker, evb_),
        acceptCallback_(this, false),
        sslAcceptCallback_(this, true),
        accepting_(true),
        shutdownPipe_(folly::make_unique<ShutdownPipe>(server, evb_)) {
  }

  folly::EventBase& eventBase() {
    return evb_;
  }

  void waitForAcceptor() {
    std::unique_lock<std::mutex> lock(acceptorLock_);
    acceptorCv_.wait(lock, [this] () { return acceptorSetup_; });
    if (spawnException_) {
      thread_.join();
      std::rethrow_exception(spawnException_);
    }
  }

  void spawn(AsyncMcServer::LoopFn fn, size_t threadId) {
    worker_.setOnShutdownOperation(
      [&] () {
        server_.shutdown();
      });

    thread_ = std::thread{
      [fn, threadId, this] (){
        if (accepting_) {
          startAccepting();

          if (spawnException_) {
            return;
          }
        }

        fn(threadId, evb_, worker_);

        // Detach the server sockets from the acceptor thread.
        // If we don't do this, the TAsyncSSLServerSocket destructor
        // will try to do it, and a segfault will result if the
        // socket destructor runs after the threads' destructors.
        if (accepting_) {
          socket_.reset();
          sslSocket_.reset();
        }
      }};
  }

  /* Safe to call from other threads */
  void shutdown() {
    auto result = evb_.runInEventBaseThread(
      [&] () {
        if (accepting_) {
          socket_.reset();
          sslSocket_.reset();
        }
        if (shutdownPipe_) {
          shutdownPipe_->unregisterHandler();
        }
        worker_.shutdown();
      });
    CHECK(result) << "error calling runInEventBaseThread";
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
  class AcceptCallback :
      public apache::thrift::async::TAsyncServerSocket::AcceptCallback {
   public:
    AcceptCallback(McServerThread* mcServerThread, bool secure)
        : mcServerThread_(mcServerThread), secure_(secure) { }
    void connectionAccepted(
        int fd,
        const apache::thrift::transport::TSocketAddress& clientAddr) noexcept {
      if (secure_) {
        auto& opts = mcServerThread_->server_.opts_;
        auto sslCtx = getSSLContext(opts.pemCertPath, opts.pemKeyPath,
                                    opts.pemCaPath);
        if (sslCtx) {
          sslCtx->setVerificationOption(
              SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
          mcServerThread_->worker_.addSecureClientSocket(fd, std::move(sslCtx));
        } else {
          ::close(fd);
        }
      } else {
        mcServerThread_->worker_.addClientSocket(fd);
      }
    }
    void acceptError(const std::exception& ex) noexcept {
      LOG(ERROR) << "Connection accept error: " << ex.what();
    }
   private:
    McServerThread* mcServerThread_{nullptr};
    bool secure_{false};
  };

  AsyncMcServer& server_;
  folly::EventBase evb_;
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

  apache::thrift::async::TAsyncServerSocket::UniquePtr socket_;
  apache::thrift::async::TAsyncServerSocket::UniquePtr sslSocket_;
  std::unique_ptr<ShutdownPipe> shutdownPipe_;

  void startAccepting() {
    CHECK(accepting_);
    try {
      auto& opts = server_.opts_;

      if (opts.existingSocketFd != -1) {
        checkLogic(opts.ports.empty() && opts.sslPorts.empty(),
                   "Can't use ports if using existing socket");
        if (!opts.pemCertPath.empty() || !opts.pemKeyPath.empty() ||
            !opts.pemCaPath.empty()) {
          checkLogic(
            !opts.pemCertPath.empty() && !opts.pemKeyPath.empty() &&
            !opts.pemCaPath.empty(),
            "All of pemCertPath, pemKeyPath and pemCaPath are required "
            "if at least one of them set");

          sslSocket_.reset(new apache::thrift::async::TAsyncServerSocket());
          sslSocket_->useExistingSocket(opts.existingSocketFd);
        } else {
          socket_.reset(new apache::thrift::async::TAsyncServerSocket());
          socket_->useExistingSocket(opts.existingSocketFd);
        }
      } else {
        checkLogic(!server_.opts_.ports.empty() ||
                   !server_.opts_.sslPorts.empty(),
                   "At least one port (plain or SSL) must be speicified");
        if (!server_.opts_.ports.empty()) {
          socket_.reset(new apache::thrift::async::TAsyncServerSocket());
          for (auto port : server_.opts_.ports) {
            socket_->bind(port);
          }
        }
        if (!server_.opts_.sslPorts.empty()) {
          checkLogic(!server_.opts_.pemCertPath.empty() &&
                     !server_.opts_.pemKeyPath.empty() &&
                     !server_.opts_.pemCaPath.empty(),
                     "All of pemCertPath, pemKeyPath, pemCaPath required"
                     " with sslPorts");

          sslSocket_.reset(new apache::thrift::async::TAsyncServerSocket());
          for (auto sslPort : server_.opts_.sslPorts) {
            sslSocket_->bind(sslPort);
          }
        }
      }

      if (socket_) {
        socket_->listen(SOMAXCONN);
        socket_->startAccepting();
        socket_->attachEventBase(&evb_);
      }
      if (sslSocket_) {
        sslSocket_->listen(SOMAXCONN);
        sslSocket_->startAccepting();
        sslSocket_->attachEventBase(&evb_);
      }

      for (auto& t : server_.threads_) {
        if (socket_ != nullptr) {
          socket_->addAcceptCallback(&t->acceptCallback_, &t->evb_);
        }
        if (sslSocket_ != nullptr) {
          sslSocket_->addAcceptCallback(&t->sslAcceptCallback_, &t->evb_);
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

AsyncMcServer::AsyncMcServer(Options opts)
    : opts_(std::move(opts)) {
}

AsyncMcServer::~AsyncMcServer() {
  /* Need to place the destructor here, since this is the only
     translation unit that knows about McServerThread */

  /* In case some signal handlers are still registered */
  gServer = nullptr;
}

void AsyncMcServer::spawn(LoopFn fn) {
  CHECK(opts_.numThreads > 0);

  threads_.emplace_back(folly::make_unique<McServerThread>(
                          McServerThread::Acceptor, *this));
  for (size_t i = 1; i < opts_.numThreads; ++i) {
    threads_.emplace_back(folly::make_unique<McServerThread>(*this));
  }

  /* We need to make sure we register all acceptor callbacks before
     running spawn() on other threads. This is so that eventBase.loop()
     never exits immediately on non-acceptor threads. */
  threads_[0]->spawn(fn, 0);
  threads_[0]->waitForAcceptor();
  for (size_t id = 1; id < threads_.size(); ++id) {
    threads_[id]->spawn(fn, id);
  }
}

void AsyncMcServer::shutdown() {
  std::lock_guard<std::mutex> lock(shutdownLock_);
  if (!alive_) {
    return;
  }

  for (auto& thread : threads_) {
    thread->shutdown();
  }
  alive_ = false;
}

void AsyncMcServer::installShutdownHandler(const std::vector<int>& signals) {
  gServer = this;
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = [] (int) {
    if (gServer) {
      gServer->shutdownFromSignalHandler();
    }
  };
  act.sa_flags = SA_RESETHAND;

  for (auto sig : signals) {
    CHECK(!sigaction(sig, &act, nullptr));
  }
}

void AsyncMcServer::shutdownFromSignalHandler() {
  if (!threads_.empty()) {
    threads_[0]->shutdownFromSignalHandler();
  }
}

void AsyncMcServer::join() {
  for (auto& thread : threads_) {
    thread->join();
  }
}

}}  // facebook::memcache
