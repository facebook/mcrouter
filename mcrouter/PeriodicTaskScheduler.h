/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace facebook { namespace memcache { namespace mcrouter {

/* Class to shedule periodic tasks. */
class PeriodicTaskScheduler {
 public:
  PeriodicTaskScheduler();

  /**
   * Creates a new thread that will run the func every tmo_ms miliseconds.
   * The scheduler first waits for tmo_ms miliseconds and then runs the
   * function for the first time.
   *
   * @param tmo_ms freq in milliseconds with which to run the function.
   * @param func the function to execute periodically.
   */
  void scheduleTask(int32_t tmo_ms,
                    std::function<void(PeriodicTaskScheduler&)> func);

  /**
   * Notifies all threads to shutdown and joins them.
   * Can only be called once. Upon exit all threads are guaranteed
   * to have been shutdown. Throws if called more than once.
   */
  void shutdownAllTasks();

  /**
   * Detaches all the threads.
   * Called from the child of the forked process. The threads don't exist
   * anymore, but std::thread's destructor will throw unless we pretend
   * they were detached. This is broken and wrong,
   * but so is forking() a multithreaded process.
   */
  void forkWorkAround();

  /**
   * Makes the thread go to sleep for the given amount
   * Will be interrupted on shutdown (returning false) - the caller
   * should interpret this to stop any processing and
   * shut down the task quickly and cleanly.
   */
  bool sleepThread(int32_t tmoMs);
 private:
  struct TaskThread {
    TaskThread(PeriodicTaskScheduler& scheduler, int32_t timeoutMs,
               std::function<void(PeriodicTaskScheduler&)> fn);
    int32_t tmoMs;
    std::function<void(PeriodicTaskScheduler&)> func;
    std::thread t;
  };
  std::mutex cvMutex_;
  std::condition_variable cv_;
  std::atomic_bool shutdown_;
  std::vector<std::unique_ptr<TaskThread>> tasks_;
  void schedulerThreadRun(TaskThread& task);
};

}}} // namespace
