/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyLogger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include "folly/DynamicConverter.h"
#include "folly/io/async/EventBase.h"
#include "folly/json.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/_router.h"
#include "mcrouter/async.h"
#include "mcrouter/proxy.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

const std::string kConfigSourcesInfoFileName = "config_sources_info";

void log_stats(const asox_timer_t timer, void* arg) {
  auto logger = reinterpret_cast<ProxyLogger*>(arg);
  logger->log();
}

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
inline std::string get_stats_key(const proxy_t* proxy) {
  return folly::format(
    "libmcrouter.{}.{}",
    proxy->opts.service_name,
    proxy->opts.router_name).str();
}

/**
 * Converts stats to a string format.
 */
std::string proxy_stats_to_string(const folly::dynamic& stats) {
  return folly::toPrettyJson(stats).toStdString() + "\n";
}

/**
 * Writes string to a file.
 */
void proxy_write_file(proxy_t* proxy,
                      const std::string& suffix,
                      const std::string& str) {
  try {
    // In case the dir was deleted some time after mcrouter started
    if (!ensure_dir_exists_and_writeable(proxy->opts.stats_root)) {
      return;
    }

    boost::filesystem::path path(proxy->opts.stats_root);
    path /= get_stats_key(proxy) + "." + suffix;

    async_write_file(proxy->stats_log_writer.get(), path.string(), str);
  } catch (...) {
    // Do nothing
  }
}

/**
 * Determines the correct location and file name and writes the stats object
 * to disk in json format. The suffix is the file extension - like "stats"
 * or "rtt_stats". If the stats root directory is ever removed or is
 * unwriteable, we just give up.
 */
void proxy_write_stats_file(proxy_t* proxy,
                            const std::string& suffix,
                            const folly::dynamic& stats) {
  proxy_write_file(proxy, suffix, proxy_stats_to_string(stats));
}

void write_stats_to_disk(proxy_t* proxy, const std::vector<stat_t>& stats) {
  try {
    std::string prefix = get_stats_key(proxy) + ".";
    folly::dynamic jstats = folly::dynamic::object;

    for (size_t i = 0; i < stats.size(); ++i) {
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

    proxy_write_stats_file(proxy, "stats", jstats);
  } catch (...) {
    // Do nothing
  }
}

void write_config_sources_info_to_disk(const proxy_t* proxy) {
  auto config_info_json = proxy->router->configApi->getConfigSourcesInfo();

  try {
    boost::filesystem::path path(proxy->opts.stats_root);
    path /= get_stats_key(proxy) + "." + kConfigSourcesInfoFileName;
    async_write_file(proxy->stats_log_writer.get(), path.string(),
                     folly::toPrettyJson(config_info_json).toStdString());
  } catch (...) {
    LOG(ERROR) << "Error occured while writing configuration info to disk";
  }
}

}  // anonymous namespace

ProxyLogger::ProxyLogger(proxy_t* proxy)
    : proxy_(proxy) {

  if (!ensure_dir_exists_and_writeable(proxy_->opts.stats_root)) {
    LOG(ERROR) << "Can't create or chmod " << proxy_->opts.stats_root <<
                  ", disabling stats logging";
  } else {
    std::string stats_startup_options_sfx("startup_options");
    auto json_options = folly::toDynamic(proxy_->opts.toDict());
    proxy_write_stats_file(proxy_, stats_startup_options_sfx, json_options);
    boost::filesystem::path path(proxy_->opts.stats_root);
    path /= get_stats_key(proxy_) + "." + stats_startup_options_sfx;
    touchStatsFilepaths_.push_back(path.string());

    timeval_t delay = to<timeval_t>(proxy_->opts.stats_logging_interval);
    statsLoggingTimer_ = asox_add_timer(proxy_->eventBase->getLibeventBase(),
                                        delay, log_stats, this);
  }
}

ProxyLogger::~ProxyLogger() {
  if (statsLoggingTimer_ != nullptr) {
    asox_remove_timer(statsLoggingTimer_);
  }
}

std::vector<stat_t> ProxyLogger::log() {
  std::vector<stat_t> stats(num_stats);
  try {
    proxy_->router->startupLock.wait();
    std::lock_guard<ShutdownLock> lg(proxy_->router->shutdownLock());
    std::lock_guard<std::mutex> guard(proxy_->stats_lock);
    std::lock_guard<ThreadReadLock> lock(proxy_->proxyThreadConfigReadLock);

    prepare_stats(proxy_, stats.data());
  } catch (const shutdown_started_exception& e) {
    return {};
  }

  for (int i = 0; i < num_stats; ++i) {
    if (stats[i].group & rate_stats) {
      stats[i].type = stat_double;
      stats[i].data.dbl = stats_rate_value(proxy_, i);
    }
  }

  write_stats_to_disk(proxy_, stats);
  write_config_sources_info_to_disk(proxy_);

  for (const auto& filepath : touchStatsFilepaths_) {
    touchFile(filepath);
  }
  return stats;
}

}}}  // facebook::memcache::mcrouter
