/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ExternalCarbonConnectionImpl.h"

#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <gflags/gflags.h>

#include <folly/ScopeGuard.h>
#include <folly/Singleton.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/synchronization/Baton.h>
#include <folly/system/ThreadName.h>

#include "mcrouter/lib/McResUtil.h"

DEFINE_int32(
    cacheclient_external_connection_threads,
    4,
    "Thread count for ExternalCarbonConnectionImpl");

namespace carbon {
namespace detail {

Client::Client(
    facebook::memcache::ConnectionOptions connectionOptions,
    ExternalCarbonConnectionImpl::Options options)
    : connectionOptions_(connectionOptions),
      options_(options),
      client_(
          folly::EventBaseManager::get()->getEventBase()->getVirtualEventBase(),
          connectionOptions_) {
  if (options_.maxOutstanding > 0) {
    counting_sem_init(&outstandingReqsSem_, options_.maxOutstanding);
  }
}

Client::~Client() {
  closeNow();
}

size_t Client::limitRequests(size_t requestsCount) {
  if (options_.maxOutstanding == 0) {
    return requestsCount;
  }

  if (options_.maxOutstandingError) {
    return counting_sem_lazy_nonblocking(&outstandingReqsSem_, requestsCount);
  } else {
    return counting_sem_lazy_wait(&outstandingReqsSem_, requestsCount);
  }
}

void Client::closeNow() {
  client_.closeNow();
}

ThreadInfo::ThreadInfo()
    : fiberManager_(
          std::make_unique<folly::fibers::EventBaseLoopController>()) {
  folly::Baton<> baton;

  thread_ = std::thread([this, &baton] {
    folly::setThreadName("mc-eccc-pool");

    folly::EventBase* evb = folly::EventBaseManager::get()->getEventBase();
    dynamic_cast<folly::fibers::EventBaseLoopController&>(
        fiberManager_.loopController())
        .attachEventBase(evb->getVirtualEventBase());
    // At this point it is safe to use fiberManager.
    baton.post();
    evb->loopForever();

    // Close all connections.
    for (auto& client : clients_) {
      client->closeNow();
    }
    clients_.clear();
  });

  // Wait until the thread is properly initialized.
  baton.wait();
}

std::weak_ptr<Client> ThreadInfo::createClient(
    facebook::memcache::ConnectionOptions connectionOptions,
    ExternalCarbonConnectionImpl::Options options) {
  return folly::fibers::await(
      [&](folly::fibers::Promise<std::weak_ptr<Client>> p) {
        fiberManager_.addTaskRemote([
          this,
          promise = std::move(p),
          connectionOptions = std::move(connectionOptions),
          options
        ]() mutable {
          auto client = std::make_shared<Client>(connectionOptions, options);
          clients_.insert(client);
          promise.setValue(client);
        });
      });
}

void ThreadInfo::releaseClient(std::weak_ptr<Client> clientWeak) {
  fiberManager_.addTaskRemote([this, clientWeak] {
    if (auto client = clientWeak.lock()) {
      clients_.erase(client);
      client->closeNow();
    }
  });
}

ThreadInfo::~ThreadInfo() {
  fiberManager_.addTaskRemote([] {
    folly::EventBaseManager::get()->getEventBase()->terminateLoopSoon();
  });
  thread_.join();
}

} // detail

namespace {

// Pool of threads, each of them has its own EventBase and FiberManager.
class ThreadPool : public std::enable_shared_from_this<ThreadPool> {
 public:
  ThreadPool() {
    threads_.reserve(FLAGS_cacheclient_external_connection_threads);
  }

  std::pair<std::weak_ptr<detail::Client>, std::weak_ptr<detail::ThreadInfo>>
  createClient(
      facebook::memcache::ConnectionOptions connectionOptions,
      ExternalCarbonConnectionImpl::Options options) {
    auto& threadInfo = [&]() -> detail::ThreadInfo& {
      std::lock_guard<std::mutex> lck(mutex_);
      // Select a thread in round robin fashion.
      size_t threadId =
          (nextThreadId_++) % FLAGS_cacheclient_external_connection_threads;

      // If it's not running yet, then we need to start it.
      if (threads_.size() <= threadId) {
        threads_.emplace_back(std::make_unique<detail::ThreadInfo>());
      }
      return *threads_[threadId];
    }();

    std::weak_ptr<detail::Client> clientPtr =
        threadInfo.createClient(connectionOptions, options);

    std::shared_ptr<detail::ThreadInfo> threadInfoShared(
        shared_from_this(), &threadInfo);

    return std::make_pair(
        std::move(clientPtr),
        std::weak_ptr<detail::ThreadInfo>(std::move(threadInfoShared)));
  }

 private:
  std::mutex mutex_;
  size_t nextThreadId_{0};
  std::vector<std::unique_ptr<detail::ThreadInfo>> threads_;
};

folly::Singleton<ThreadPool> threadPool;

} // anonymous namespace

ExternalCarbonConnectionImpl::Impl::Impl(
    facebook::memcache::ConnectionOptions connectionOptions,
    ExternalCarbonConnectionImpl::Options options) {
  auto pool = threadPool.try_get();

  auto info = pool->createClient(connectionOptions, options);
  client_ = info.first;
  threadInfo_ = info.second;
}

ExternalCarbonConnectionImpl::Impl::~Impl() {
  if (auto threadInfo = threadInfo_.lock()) {
    threadInfo->releaseClient(client_);
  }
}

bool ExternalCarbonConnectionImpl::Impl::healthCheck() {
  folly::fibers::Baton baton;
  bool ret = false;

  auto clientWeak = client_;
  auto threadInfo = threadInfo_.lock();
  if (!threadInfo) {
    throw CarbonConnectionRecreateException(
        "Singleton<ThreadPool> was destroyed!");
  }

  threadInfo->addTaskRemote([clientWeak, &baton, &ret]() {
    auto client = clientWeak.lock();
    if (!client) {
      baton.post();
      return;
    }

    auto reply = client->sendRequest(facebook::memcache::McVersionRequest());
    ret = !facebook::memcache::isErrorResult(reply.result());
    baton.post();
  });

  baton.wait();
  return ret;
}

ExternalCarbonConnectionImpl::ExternalCarbonConnectionImpl(
    facebook::memcache::ConnectionOptions connectionOptions,
    Options options)
    : connectionOptions_(std::move(connectionOptions)),
      options_(std::move(options)),
      impl_(std::make_unique<Impl>(connectionOptions_, options_)) {}

bool ExternalCarbonConnectionImpl::healthCheck() {
  try {
    return impl_->healthCheck();
  } catch (const CarbonConnectionRecreateException&) {
    impl_ = std::make_unique<Impl>(connectionOptions_, options_);
    return impl_->healthCheck();
  }
}
} // carbon
