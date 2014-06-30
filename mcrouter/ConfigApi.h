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

  static const std::string kAbsoluteFilePrefix;

  explicit ConfigApi(const McrouterOptions& opts);

  /**
   * Subscribe a callback that will be called whenever some files changed
   *
   * @return Callback handle that is used to unsubsribe. Once the handle is
   *         destroyed provided callback won't be called anymore.
   */
  CallbackHandle subscribe(Callback callback);

  /**
   * Reads config from 'path', remembers it as one of configuration files and
   * starts watching for updates.
   *
   * @return true on success, false otherwise
   */
  virtual bool get(ConfigType type, const std::string& path,
                   std::string& contents);

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
  virtual void stopObserving(pid_t pid);

  virtual ~ConfigApi();

 protected:
  const McrouterOptions& opts_;
  CallbackPool<> callbacks_;

  /**
   * @return true, if files have update since last call, false otherwise
   */
  virtual bool checkFileUpdate();
  /**
   * @return true, if we need to create configuration thread, false otherwise.
   */
  virtual bool needConfigThread();

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
  std::thread configThread_;
  std::mutex fileInfoMutex_;
  std::atomic<bool> finish_;

  void configThreadRun();
};

}}} // facebook::memcache::mcrouter
