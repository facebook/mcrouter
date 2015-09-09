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
#include <string>
#include <thread>
#include <vector>

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterInstance;
class stat_t;

class AdditionalLoggerIf {
public:
  virtual ~AdditionalLoggerIf() {}

  virtual void log(const std::vector<stat_t>& stats) = 0;
};

class McrouterLogger {
 public:
  /*
   * Constructs a McrouterLogger for the specified router.
   *
   * @param router The router to log for.
   * @param additionalLogger Additional logger that is called everytime a log
   *                         is written.
   */
  explicit McrouterLogger(
    McrouterInstance& router,
    std::unique_ptr<AdditionalLoggerIf> additionalLogger = nullptr);

  ~McrouterLogger();

  /**
   * Starts the logger thread.
   *
   * @return True if logger thread was successfully started, false otherwise.
   */
  bool start();

  /**
   * Tells whether the logger thread is running.
   *
   * @return True if logger thread is running, false otherwise.
   */
  bool running() const;

  /**
   * Stops the logger thread and join it.
   * Note: this is a blocking call.
   */
  void stop() noexcept;

 private:
  McrouterInstance& router_;

  std::unique_ptr<AdditionalLoggerIf> additionalLogger_;

  /**
   * File paths of stats we want to touch and keep their mtimes up-to-date
   */
  std::vector<std::string> touchStatsFilepaths_;

  pid_t pid_;
  std::thread loggerThread_;
  std::mutex loggerThreadMutex_;
  std::condition_variable loggerThreadCv_;
  std::atomic<bool> running_{false};
  void loggerThreadRun();
  void loggerThreadSleep();

  /**
   * Writes router's logs.
   */
  void log();

  /**
   * Writes startup options.
   */
  void logStartupOptions();
};

}}}  // facebook::memcache::mcrouter
