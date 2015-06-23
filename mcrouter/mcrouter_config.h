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

#ifndef HAVE_CONFIG_H
static_assert(false, "mcrouter: invalid build");
#endif

/**
 * This header contains features specific for open source
 */
#include <time.h>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <folly/Range.h>
#include <folly/io/async/EventBase.h>

#define MCROUTER_RUNTIME_VARS_DEFAULT ""
#define MCROUTER_STATS_ROOT_DEFAULT "/var/mcrouter/stats"
#define MCROUTER_INSTALL_PATH ""

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

class McReplyBase;
class McrouterOptions;

using LogPostprocessCallbackFunc =
  std::function<
    void(
      folly::StringPiece, // Key requested
      uint64_t flags, // Reply flags
      folly::StringPiece, // The value in the reply
      const char* const, // Name of operation (e.g. 'get')
      const folly::StringPiece)>; // User ip

template <class T>
inline LogPostprocessCallbackFunc getLogPostprocessFunc() {
    return nullptr;
}

namespace mcrouter {

class ConfigApi;
class ExtraRouteHandleProviderIf;
class McrouterInstance;
class McrouterLogger;
class McrouterStandaloneOptions;
class proxy_t;
class TkoLog;

struct ProxyStatsContainer {
  explicit ProxyStatsContainer(proxy_t*) {}
};

struct AdditionalProxyRequestLogger {
  explicit AdditionalProxyRequestLogger(proxy_t*) {}
  /**
   * Called once a reply is received to record a stats sample if required.
   */
  template <typename... Args>
  void log(Args&&...) {
  }
};

/**
 * @return monotonic time suitable for measuring intervals in microseconds.
 */
inline int64_t nowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * @return monotonic time suitable for measuring intervals in seconds.
 */
inline double nowSec() {
  return nowUs() / 1000000.0;
}

/**
 * @return wall clock time since epoch in seconds.
 */
inline time_t nowWallSec() {
  return time(nullptr);
}

int router_configure_from_string(McrouterInstance& router,
                                 folly::StringPiece input);

bool read_standalone_flavor(
    const std::string& flavor,
    std::unordered_map<std::string, std::string>& option_dict,
    std::unordered_map<std::string, std::string>& st_option_dict);

std::unique_ptr<ConfigApi> createConfigApi(const McrouterOptions& opts);

std::string performOptionSubstitution(std::string str);

bool standaloneInit(const McrouterOptions& opts,
                    const McrouterStandaloneOptions& standaloneOpts);

std::unique_ptr<ExtraRouteHandleProviderIf> createExtraRouteHandleProvider();

std::unique_ptr<McrouterLogger> createMcrouterLogger(McrouterInstance& router);

void applyTestMode(McrouterOptions& opts);

McrouterOptions defaultTestOptions();

std::vector<std::string> defaultTestCommandLineArgs();

void logTkoEvent(proxy_t* proxy, const TkoLog& tkoLog);

void initFailureLogger();

void scheduleSingletonCleanup();

std::unordered_map<std::string, folly::dynamic> additionalConfigParams();

inline bool isMetagetAvailable() {
  return false;
}

#ifdef PACKAGE_NAME
  #define MCROUTER_PACKAGE_NAME PACKAGE_NAME
#else
  #define MCROUTER_PACKAGE_NAME "mcrouter"
#endif

#ifdef PACKAGE_VERSION
  #define MCROUTER_PACKAGE_VERSION PACKAGE_VERSION
#else
  #define MCROUTER_PACKAGE_VERSION "1.0.0"
#endif

#ifdef PACKAGE_STRING
  #define MCROUTER_PACKAGE_STRING PACKAGE_STRING
#else
  #define MCROUTER_PACKAGE_STRING MCROUTER_PACKAGE_NAME " " MCROUTER_PACKAGE_VERSION
#endif

#define MCROUTER_VERSION_STRING MCROUTER_PACKAGE_VERSION " " MCROUTER_PACKAGE_NAME

}}} // facebook::memcache::mcrouter
