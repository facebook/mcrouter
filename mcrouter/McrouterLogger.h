/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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

class mcrouter_t;
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
    mcrouter_t* router,
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
   * Dirty the config, which means the config will be written to disk next
   * time the logger runs.
   */
  void dirtyConfig();

  /**
   * Stops the logger thread and join it.
   * Note: this is a blocking call.
   */
  void stop();

 private:
  mcrouter_t* router_;

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
  std::atomic<bool> configDirty_{true};
  void loggerThreadRun();
  void loggerThreadSleep();
  void writeConfigSourcesInfo();

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
