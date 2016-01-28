/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FifoManager.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <folly/Format.h>

namespace facebook { namespace memcache {

namespace {

folly::Singleton<FifoManager> gFifoManager;

pid_t gettid() {
  return (pid_t) syscall (SYS_gettid);
}

} // anonymous namespace

FifoManager::FifoManager() {
  // Handle broken pipes on write syscalls.
  signal(SIGPIPE, SIG_IGN);

  thread_ = std::thread([this]() {
    while (true) {
      {
        folly::SharedMutex::ReadHolder lockGuard(fifosMutex_);
        for (auto& it : fifos_) {
          it.second->tryConnect();
        }
      }

      {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait_for(lk, std::chrono::milliseconds(1000),
                     [this]() { return !running_; });
        if (!running_) {
          break;
        }
      }
    }
  });
}

FifoManager::~FifoManager() {
  {
    std::unique_lock<std::mutex> lk(mutex_);
    running_ = false;
    cv_.notify_all();
  }
  thread_.join();
}

std::shared_ptr<Fifo> FifoManager::fetch(const std::string& fifoPath) {
  if (auto debugFifo = find(fifoPath)) {
    return debugFifo;
  }
  return createAndStore(fifoPath);
}

std::shared_ptr<Fifo> FifoManager::fetchThreadLocal(
    const std::string& fifoBasePath) {
  CHECK(!fifoBasePath.empty()) << "Fifo base path must not be empty";

  return fetch(folly::sformat("{0}.{1}", fifoBasePath, gettid()));
}

std::shared_ptr<Fifo> FifoManager::find(const std::string& fifoPath) {
  folly::SharedMutex::ReadHolder lockGuard(fifosMutex_);
  auto it = fifos_.find(fifoPath);
  if (it != fifos_.end()) {
    return it->second;
  }
  return nullptr;
}

std::shared_ptr<Fifo> FifoManager::createAndStore(const std::string& fifoPath) {
  folly::SharedMutex::WriteHolder lockGuard(fifosMutex_);
  auto it = fifos_.emplace(fifoPath, std::shared_ptr<Fifo>(new Fifo(fifoPath)));
  return it.first->second;
}

void FifoManager::clear() {
  folly::SharedMutex::WriteHolder lockGuard(fifosMutex_);
  fifos_.clear();
}

/* static  */ std::shared_ptr<FifoManager> FifoManager::getInstance() {
  return folly::Singleton<FifoManager>::try_get();
}

}} // facebook::memcache
