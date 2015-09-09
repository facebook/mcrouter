/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ConfigApi.h"

#include <boost/filesystem/path.hpp>

#include <folly/dynamic.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>

#include "mcrouter/config.h"
#include "mcrouter/FileDataProvider.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/ThreadUtil.h"

namespace facebook { namespace memcache { namespace mcrouter {

const char* const kMcrouterConfigKey = "mcrouter_config";
const char* const kConfigFile = "config_file";
const char* const kConfigImport = "config_import";
const int kConfigReloadInterval = 60;

const char* const ConfigApi::kAbsoluteFilePrefix = "file:";

ConfigApi::~ConfigApi() {
  /* Must be here to forward declare FileDataProvider */
}

ConfigApi::ConfigApi(const McrouterOptions& opts)
    : opts_(opts),
      finish_(false) {
}

ConfigApi::CallbackHandle ConfigApi::subscribe(Callback callback) {
  return callbacks_.subscribe(std::move(callback));
}

void ConfigApi::startObserving() {
  assert(!finish_);
  if (!opts_.disable_reload_configs) {
    configThread_ = std::thread(std::bind(&ConfigApi::configThreadRun, this));
  }
}

void ConfigApi::stopObserving(pid_t pid) noexcept {
  {
    std::lock_guard<std::mutex> lk(finishMutex_);
    finish_ = true;
    finishCV_.notify_one();
  }
  if (configThread_.joinable()) {
    if (getpid() == pid) {
      configThread_.join();
    } else {
      configThread_.detach();
    }
  }
}

bool ConfigApi::checkFileUpdate() {
  auto now = nowWallSec();
  bool hasUpdate = false;
  std::lock_guard<std::mutex> lock(fileInfoMutex_);
  for (auto& fileIt : fileInfos_) {
    auto& file = fileIt.second;
    // hasUpdate reads events from inotify, so we need to poll all
    // providers to reconfigure only once when multiple files have changed
    try {
      if (file.provider) {
        hasUpdate |= file.provider->hasUpdate();
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Check " << file.path << " for update failed: "
                 << e.what();
      // check with hash, if it throws error something is totally wrong,
      // reconfiguration thread will log error and finish.
      file.provider.reset();
      if (file.lastMd5Check + kConfigReloadInterval < now) {
        hasUpdate |= file.checkMd5Changed();
      }
    }
  }
  return hasUpdate;
}

void ConfigApi::configThreadRun() {
  mcrouterSetThisThreadName(opts_, "mcrcfg");
  if (opts_.constantly_reload_configs) {
    while (!finish_) {
      LOG(INFO) << "Reload config due to constantly_reload_configs";
      callbacks_.notify();
      {
        std::unique_lock<std::mutex> lk(finishMutex_);
        finishCV_.wait_for(lk, std::chrono::milliseconds(10),
                           [this] { return finish_.load(); });

      }
    }
    return;
  }

  while (!finish_) {
    bool hasUpdate = false;
    try {
      hasUpdate = checkFileUpdate();
    } catch (const std::exception& e) {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kOther,
                     "Check for config update failed: {}", e.what());
    } catch (...) {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kOther,
                     "Check for config update failed with unknown error");
    }
    // There are a couple of races that can happen here
    // First, the IN_MODIFY event can be fired before the write is complete,
    // resulting in a malformed JSON error. Second, text editors may do
    // some of their own shuffling of the file (e.g. between .swp and the
    // real thing in Vim) after the write. This may can result in a file
    // access error router_configure_from_file below. That's just a theory,
    // but that error does happen. Race 1 can be fixed by changing the
    // watch for IN_MODIFY to IN_CLOSE_WRITE, but Race 2 has no apparent
    // elegant solution. The following jankiness fixes both.

    {
      std::unique_lock<std::mutex> lk(finishMutex_);
      finishCV_.wait_for(
        lk, std::chrono::milliseconds(opts_.reconfiguration_delay_ms),
        [this] { return finish_.load(); });

    }

    if (hasUpdate) {
      callbacks_.notify();
    }

    // Otherwise there was nothing to read, so check that we aren't shutting
    // down, and wait on the FD again.
  }
}

bool ConfigApi::FileInfo::checkMd5Changed() {
  auto previousHash = md5;
  std::string contents;
  if (!folly::readFile(path.data(), contents)) {
    throw std::runtime_error("Error reading from config file " + path);
  }
  md5 = Md5Hash(contents);
  lastMd5Check = nowWallSec();

  return md5 != previousHash;
}

bool ConfigApi::getConfigFile(std::string& contents) {
  if (!opts_.config_str.empty()) {
    // explicit config, no automatic reload
    contents = opts_.config_str;
    return true;
  }
  if (!opts_.config_file.empty()) {
    return get(ConfigType::ConfigFile, opts_.config_file, contents);
  }
  return false;
}

bool ConfigApi::get(ConfigType type, const std::string& path,
                    std::string& contents) {
  std::string fullPath;
  if (type == ConfigType::ConfigImport) {
    if (folly::StringPiece(path).startsWith(kAbsoluteFilePrefix)) {
      fullPath = path.substr(strlen(kAbsoluteFilePrefix));
    } else {
      boost::filesystem::path filePath(opts_.config_file);
      fullPath = (filePath.parent_path() / path).string();
    }
  } else if (type == ConfigType::Pool) {
    if (folly::StringPiece(path).startsWith(kAbsoluteFilePrefix)) {
      fullPath = path.substr(strlen(kAbsoluteFilePrefix));
    } else {
      return false;
    }
  } else {
    fullPath = path;
  }

  if (!folly::readFile(fullPath.data(), contents)) {
    return false;
  }

  if (tracking_) {
    std::lock_guard<std::mutex> lock(fileInfoMutex_);
    auto fileInfoIt = trackedFiles_.emplace(fullPath, FileInfo()).first;

    auto& file = fileInfoIt->second;
    file.path = fullPath;
    file.type = type;
    file.lastMd5Check = nowWallSec();
    file.md5 = Md5Hash(contents);
  }
  return true;
}

void ConfigApi::trackConfigSources() {
  std::lock_guard<std::mutex> lock(fileInfoMutex_);
  tracking_ = true;
}

void ConfigApi::subscribeToTrackedSources() {
  std::lock_guard<std::mutex> lock(fileInfoMutex_);
  assert(tracking_);
  tracking_ = false;
  isFirstConfig_ = false;

  if (!opts_.disable_reload_configs) {
    // start watching for updates
    for (auto& it : trackedFiles_) {
      auto& file = it.second;
      try {
        if (!file.provider) {
          // reuse existing providers
          auto fileInfoIt = fileInfos_.find(file.path);
          if (fileInfoIt != fileInfos_.end()) {
            file.provider = std::move(fileInfoIt->second.provider);
          }
        }
        if (!file.provider) {
          file.provider = folly::make_unique<FileDataProvider>(file.path);
        }
      } catch (const std::exception& e) {
        // it's not that bad, we will check for change in hash
        LOG(INFO) << "Can not start watching " << file.path <<
                     " for modifications: " << e.what();
      }
    }
  }

  fileInfos_ = std::move(trackedFiles_);
  trackedFiles_.clear();
}

void ConfigApi::abandonTrackedSources() {
  std::lock_guard<std::mutex> lock(fileInfoMutex_);
  assert(tracking_);
  tracking_ = false;
  trackedFiles_.clear();
}

folly::dynamic ConfigApi::getConfigSourcesInfo() {
  folly::dynamic reply_val = folly::dynamic::object;

  // we have config_str, write its hash
  if (!opts_.config_str.empty()) {
    reply_val[kMcrouterConfigKey] = Md5Hash(opts_.config_str);
  }

  std::lock_guard<std::mutex> lock(fileInfoMutex_);

  for (const auto& fileIt : fileInfos_) {
    const auto& file = fileIt.second;
    switch (file.type) {
      case ConfigType::ConfigFile:
        reply_val[kConfigFile] = file.path;
        reply_val[kMcrouterConfigKey] = file.md5;
        break;
      case ConfigType::Pool:
        if (!reply_val.count("pools")) {
          reply_val["pools"] = folly::dynamic::object;
        }
        reply_val["pools"].insert(file.path, file.md5);
        break;
      case ConfigType::ConfigImport:
        if (!reply_val.count(kConfigImport)) {
          reply_val[kConfigImport] = folly::dynamic::object;
        }
        reply_val[kConfigImport].insert(file.path, file.md5);
        break;
    }
  }

  return reply_val;
}

bool ConfigApi::isFirstConfig() const {
  return isFirstConfig_;
}

}}} // facebook::memcache::mcrouter
