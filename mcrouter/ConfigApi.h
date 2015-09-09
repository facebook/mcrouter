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
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "mcrouter/CallbackPool.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

class FileDataProvider;

enum class ConfigType {
  ConfigFile = 0,
  ConfigImport = 1,
  Pool = 2
};

/**
 * Incapsulates logic of fetching configs from files.
 */
class ConfigApi {
 public:
  typedef std::function<void()> Callback;
  typedef CallbackPool<>::CallbackHandle CallbackHandle;

  static const char* const kAbsoluteFilePrefix;

  explicit ConfigApi(const McrouterOptions& opts);

  /**
   * Subscribe a callback that will be called whenever some files changed
   *
   * @return Callback handle that is used to unsubsribe. Once the handle is
   *         destroyed provided callback won't be called anymore.
   */
  CallbackHandle subscribe(Callback callback);

  /**
   * Reads config from 'path'.
   *
   * @return true on success, false otherwise
   */
  virtual bool get(ConfigType type, const std::string& path,
                   std::string& contents);

  /**
   * All files we 'get' after this call will be marked as 'tracked'. Once
   * we call 'subscribeToTrackedSources', we'll receive updates only for
   * 'tracked' files.
   */
  void trackConfigSources();

  /**
   * Changes set of files we're observing to 'tracked' files.
   */
  virtual void subscribeToTrackedSources();

  /**
   * Discard 'tracked' files, keep observing files we had before
   * 'trackConfigSources' call.
   */
  virtual void abandonTrackedSources();

  /**
   * Reads configuration file according to mcrouter options.
   *
   * @param[out] config Will contain contents of configuration file on success
   * @return true on success, false otherwise
   */
  virtual bool getConfigFile(std::string& config);

  /**
   * @return dynamic object with information about files used in configuration.
   */
  virtual folly::dynamic getConfigSourcesInfo();

  /**
   * Starts observing for file changes
   */
  virtual void startObserving();

  /**
   * Stops observing for file changes
   */
  virtual void stopObserving(pid_t pid) noexcept;

  virtual ~ConfigApi();

 protected:
  const McrouterOptions& opts_;
  CallbackPool<> callbacks_;
  std::atomic<bool> tracking_;

  /**
   * Informs whether this is the first time mcrouter is being configured.
   */
  bool isFirstConfig() const;

  /**
   * @return true, if files have update since last call, false otherwise
   */
  virtual bool checkFileUpdate();

 private:
  struct FileInfo {
    std::string path;
    std::string md5;
    ConfigType type{ConfigType::ConfigFile};
    std::unique_ptr<FileDataProvider> provider;
    time_t lastMd5Check{0}; // last hash check in seconds since epoch

    bool checkMd5Changed();
  };
  /// file path -> FileInfo
  std::unordered_map<std::string, FileInfo> fileInfos_;
  std::unordered_map<std::string, FileInfo> trackedFiles_;

  std::thread configThread_;
  std::mutex fileInfoMutex_;

  std::mutex finishMutex_;
  std::condition_variable finishCV_;
  std::atomic<bool> finish_;

  bool isFirstConfig_{true};

  void configThreadRun();
};

}}} // facebook::memcache::mcrouter
