/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McrouterLogger.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <folly/DynamicConverter.h>
#include <folly/json.h>
#include <folly/ThreadName.h>

#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

const char* kStatsSfx = "stats";
const char* kStatsStartupOptionsSfx = "startup_options";
const char* kConfigSourcesInfoFileName = "config_sources_info";

bool ensure_dir_exists_and_writeable(const std::string& path) {
  boost::system::error_code ec;
  boost::filesystem::create_directories(path, ec);
  if (ec) {
    return false;
  }

  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }

  if ((st.st_mode & 0777) == 0777) {
    return true;
  }

  if (::chmod(path.c_str(), 0777) != 0) {
    return false;
  }

  return true;
}

/** returns "<source_library>.<source_service>.<source_flavor>" */
inline std::string get_stats_key(const McrouterOptions& opts) {
  return folly::format(
    "libmcrouter.{}.{}",
    opts.service_name,
    opts.router_name).str();
}

std::string stats_file_path(const McrouterOptions& opts,
                            const std::string& suffix) {
  boost::filesystem::path path(opts.stats_root);
  path /= get_stats_key(opts) + "." + suffix;
  return path.string();
}

/**
 * Writes string to a file.
 */
void write_file(const McrouterOptions& opts,
                const std::string& suffix,
                const std::string& str) {
  try {
    // In case the dir was deleted some time after mcrouter started
    if (!ensure_dir_exists_and_writeable(opts.stats_root)) {
      return;
    }

    std::string path = stats_file_path(opts, suffix);
    atomicallyWriteFileToDisk(str, path);
  } catch (...) {
    // Do nothing
  }
}

/**
 * Determines the correct location and file name and writes the stats object
 * to disk in json format. The suffix is the file extension. If the stats root
 * directory is ever removed or is unwriteable, we just give up.
 */
void write_stats_file(const McrouterOptions& opts,
                      const std::string& suffix,
                      const folly::dynamic& stats) {
  auto statsString = folly::toPrettyJson(stats).toStdString() + "\n";
  write_file(opts, suffix, statsString);
}

void write_stats_to_disk(const McrouterOptions& opts,
                         const std::vector<stat_t>& stats) {
  try {
    std::string prefix = get_stats_key(opts) + ".";
    folly::dynamic jstats = folly::dynamic::object;

    for (size_t i = 0; i < stats.size(); ++i) {
      if (opts.logging_rtt_outlier_threshold_us == 0 &&
          (stats[i].group & outlier_stats)) {
        // outlier detection is disabled
        continue;
      }
      if (stats[i].group & ods_stats) {
        auto key = prefix + stats[i].name.str();

        switch (stats[i].type) {
          case stat_uint64:
            jstats[key] = stats[i].data.uint64;
            break;

          case stat_int64:
            jstats[key] = stats[i].data.int64;
            break;

          case stat_double:
            jstats[key] = stats[i].data.dbl;
            break;

          default:
            continue;
        }
      }
    }

    write_stats_file(opts, kStatsSfx, jstats);
  } catch (...) {
    // Do nothing
  }
}

void write_config_sources_info_to_disk(McrouterInstance* router) {
  auto config_info_json = router->configApi().getConfigSourcesInfo();

  try {
    boost::filesystem::path path(router->opts().stats_root);
    path /= get_stats_key(router->opts()) + "." + kConfigSourcesInfoFileName;
    atomicallyWriteFileToDisk(
      folly::toPrettyJson(config_info_json).toStdString(),
      path.string());
  } catch (...) {
    LOG(ERROR) << "Error occured while writing configuration info to disk";
  }
}

}  // anonymous namespace

McrouterLogger::McrouterLogger(McrouterInstance* router,
  std::unique_ptr<AdditionalLoggerIf> additionalLogger)
    : router_(router),
      additionalLogger_(std::move(additionalLogger)),
      pid_(getpid()) {
}

McrouterLogger::~McrouterLogger() {
  stop();
}

bool McrouterLogger::start() {
  if (running_ || router_->opts().stats_logging_interval == 0) {
    return false;
  }

  if (!ensure_dir_exists_and_writeable(router_->opts().stats_root)) {
    LOG(ERROR) << "Can't create or chmod " << router_->opts().stats_root <<
                  ", disabling stats logging";
    return false;
  }

  auto path = stats_file_path(router_->opts(), kStatsStartupOptionsSfx);
  if (std::find(touchStatsFilepaths_.begin(), touchStatsFilepaths_.end(), path)
      == touchStatsFilepaths_.end()) {
    touchStatsFilepaths_.push_back(std::move(path));
  }

  running_ = true;
  const std::string threadName = "mcrtr-stats-logger";
  try {
    loggerThread_ = std::thread(
        std::bind(&McrouterLogger::loggerThreadRun, this));
    folly::setThreadName(loggerThread_.native_handle(), threadName);
  } catch (const std::system_error& e) {
    running_ = false;
    logFailure(router_, memcache::failure::Category::kSystemError,
               "Can not start McrouterLogger thread {}: {}",
               threadName, e.what());
  }

  return running_;
}

void McrouterLogger::stop() {
  if (!running_) {
    return;
  }

  running_ = false;
  loggerThreadCv_.notify_all();
  if (loggerThread_.joinable()) {
    if (getpid() == pid_) {
      loggerThread_.join();
    } else {
      loggerThread_.detach();
    }
  }
}

bool McrouterLogger::running() const {
  return running_;
}

void McrouterLogger::loggerThreadRun() {
  logStartupOptions();

  while (running_) {
    log();
    loggerThreadSleep();
  }
}

void McrouterLogger::loggerThreadSleep() {
  std::unique_lock<std::mutex> lock(loggerThreadMutex_);
  loggerThreadCv_.wait_for(
    lock,
    std::chrono::milliseconds(router_->opts().stats_logging_interval),
    [this]() { return !running_; });
}

void McrouterLogger::logStartupOptions() {
  auto json_options = folly::toDynamic(router_->getStartupOpts());
  json_options["pid"] = folly::to<std::string>(getpid());
  write_stats_file(router_->opts(), kStatsStartupOptionsSfx, json_options);
}

void McrouterLogger::log() {
  std::vector<stat_t> stats(num_stats);
  try {
    std::lock_guard<ShutdownLock> lg(router_->shutdownLock());

    prepare_stats(router_, stats.data());
  } catch (const shutdown_started_exception& e) {
    return;
  }

  for (int i = 0; i < num_stats; ++i) {
    if (stats[i].group & rate_stats) {
      stats[i].type = stat_double;
      stats[i].data.dbl = stats_aggregate_rate_value(router_, i);
    }
  }

  write_stats_to_disk(router_->opts(), stats);
  write_config_sources_info_to_disk(router_);

  for (const auto& filepath : touchStatsFilepaths_) {
    touchFile(filepath);
  }

  if (additionalLogger_) {
    additionalLogger_->log(stats);
  }
}

}}}  // facebook::memcache::mcrouter
