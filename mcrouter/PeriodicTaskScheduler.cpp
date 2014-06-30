/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "PeriodicTaskScheduler.h"

#include "folly/Memory.h"
#include "folly/ThreadName.h"
#include "mcrouter/lib/fbi/debug.h"

namespace facebook { namespace memcache { namespace mcrouter {

PeriodicTaskScheduler::TaskThread::TaskThread(
    PeriodicTaskScheduler& scheduler,
    int32_t timeoutMs,
    std::function<void(PeriodicTaskScheduler&)> fn)
  :  tmoMs(timeoutMs),
     func(std::move(fn)),
     t{[this, &scheduler] () {
       folly::setThreadName("mcrtr-periodic");
       scheduler.schedulerThreadRun(*this);
      }} {}

void PeriodicTaskScheduler::scheduleTask(
    int32_t tmoMs,
    std::function<void(PeriodicTaskScheduler&)> func) {
  tasks_.push_back(
      folly::make_unique<TaskThread>(*this, tmoMs, std::move(func)));
}

PeriodicTaskScheduler::PeriodicTaskScheduler()
  : shutdown_(false) {}

void PeriodicTaskScheduler::shutdownAllTasks() {
  if (shutdown_.exchange(true)) {
    throw std::runtime_error("Received a second call on shutdownAllTasks.");
  }
  cv_.notify_all();
  for (auto& task: tasks_) {
    task->t.join();
  }
}

void PeriodicTaskScheduler::forkWorkAround() {
  for (auto& task: tasks_) {
    task->t.detach();
  }
}

void PeriodicTaskScheduler::schedulerThreadRun(TaskThread& task) {
  while (sleepThread(task.tmoMs)) {
    try {
      task.func(*this);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Error while executing periodic function: " << ex.what();
      break;
    } catch (...) {
      LOG(ERROR) << "Unknown error occured while executing periodic function";
      break;
    }
  }
}

bool PeriodicTaskScheduler::sleepThread(int32_t tmoMs) {
  std::unique_lock<std::mutex> lock(cvMutex_);
  return !cv_.wait_for(
      lock,
      std::chrono::milliseconds(tmoMs),
      [this]() { return this->shutdown_.load(); });
}

}}} // namespace
