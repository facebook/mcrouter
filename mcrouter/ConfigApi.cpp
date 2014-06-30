/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ConfigApi.h"

#include <boost/filesystem/path.hpp>

#include "folly/FileUtil.h"
#include "folly/Memory.h"
#include "folly/dynamic.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/_router.h"
#include "mcrouter/FileDataProvider.h"
#include "mcrouter/config.h"
#include "mcrouter/options.h"

namespace facebook { namespace memcache { namespace mcrouter {

const std::string kMcrouterConfigKey = "mcrouter_config";
const std::string kConfigFile = "config_file";
const std::string kConfigImport = "config_import";
const int kConfigReloadInterval = 60;

const std::string ConfigApi::kAbsoluteFilePrefix = "file:";

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
  if (needConfigThread()) {
    configThread_ = std::thread(std::bind(&ConfigApi::configThreadRun, this));
  }
}

bool ConfigApi::needConfigThread() {
  if (opts_.disable_reload_configs) {
    return false;
  }

  // no auto-config if we were only given a str
  return !opts_.config_file.empty() || opts_.constantly_reload_configs;
}

void ConfigApi::stopObserving(pid_t pid) {
  finish_ = true;
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
  mcrouter_set_thread_name(pthread_self(), opts_, "mcrcfg");
  if (opts_.constantly_reload_configs) {
    while (!finish_) {
      LOG(INFO) << "Reload config due to constantly_reload_configs";
      callbacks_.notify();
      usleep(10000);
    }
    return;
  }

  while (!finish_) {
    bool hasUpdate;
    try {
      hasUpdate = checkFileUpdate();
    } catch (const std::exception& e) {
      LOG(ERROR) << "Check for config update failed: " << e.what();
      return;
    } catch (...) {
      LOG(ERROR) << "Check for config update failed with unknown error";
      return;
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
    sleep(1);

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
      fullPath = path.substr(kAbsoluteFilePrefix.length());
    } else {
      boost::filesystem::path filePath(opts_.config_file);
      fullPath = (filePath.parent_path() / path).string();
    }
  } else if (type == ConfigType::Pool) {
    if (folly::StringPiece(path).startsWith(kAbsoluteFilePrefix)) {
      fullPath = path.substr(kAbsoluteFilePrefix.length());
    } else {
      return false;
    }
  } else {
    fullPath = path;
  }

  if (!folly::readFile(fullPath.data(), contents)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(fileInfoMutex_);
    auto fileInfoIt = fileInfos_.find(fullPath);
    if (fileInfoIt == fileInfos_.end()) {
      fileInfoIt = fileInfos_.emplace(fullPath, FileInfo()).first;
    }

    auto& file = fileInfoIt->second;
    file.path = fullPath;
    file.type = type;
    file.lastMd5Check = nowWallSec();
    file.md5 = Md5Hash(contents);
    try {
      if (!file.provider) {
        file.provider = folly::make_unique<FileDataProvider>(file.path);
      }
    } catch (const std::exception& e) {
      // it's not that bad, we will check for change in hash
      LOG(INFO) << "Can not start watching " << file.path <<
                   " for modifications: " << e.what();
    }
  }
  return true;
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

}}} // facebook::memcache::mcrouter
