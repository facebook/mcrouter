/*
 *  Copyright (c) 2016, Facebook, Inc.
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
#define DEBUG_FIFO_ROOT_DEFAULT "/var/mcrouter/fifos"

namespace folly {
struct dynamic;
} // folly

namespace facebook { namespace memcache {

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
struct FailoverContext;
struct proxy_t;
struct RequestLoggerContext;
struct ShadowValidationData;
struct TkoLog;

struct ProxyStatsContainer {
  explicit ProxyStatsContainer(proxy_t&) {}
};

struct AdditionalProxyRequestLogger {
  explicit AdditionalProxyRequestLogger(proxy_t*) {}
  /**
   * Called once a reply is received to record a stats sample if required.
   */
  void log(const RequestLoggerContext&) {
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

bool read_standalone_flavor(
    const std::string& flavor,
    std::unordered_map<std::string, std::string>& option_dict,
    std::unordered_map<std::string, std::string>& st_option_dict);

std::unique_ptr<ConfigApi> createConfigApi(const McrouterOptions& opts);

std::string performOptionSubstitution(std::string str);

inline void standaloneInit(const McrouterOptions& opts,
                           const McrouterStandaloneOptions& standaloneOpts) {
}

std::unique_ptr<ExtraRouteHandleProviderIf> createExtraRouteHandleProvider();

std::unique_ptr<McrouterLogger> createMcrouterLogger(McrouterInstance& router);

/**
 * @throws logic_error on invalid options
 */
void extraValidateOptions(const McrouterOptions& opts);

void applyTestMode(McrouterOptions& opts);

McrouterOptions defaultTestOptions();

std::vector<std::string> defaultTestCommandLineArgs();

void logTkoEvent(proxy_t& proxy, const TkoLog& tkoLog);

void logFailover(proxy_t& proxy, const FailoverContext& failoverContext);

void logShadowValidationError(proxy_t& proxy,
                              const ShadowValidationData& valData);

void initFailureLogger();

void scheduleSingletonCleanup();

std::unordered_map<std::string, folly::dynamic> additionalConfigParams();

inline bool isMetagetAvailable() {
  return false;
}

void insertCustomStartupOpts(folly::dynamic& options);

std::string getBinPath(folly::StringPiece name);

#ifndef MCROUTER_PACKAGE_STRING
  #define MCROUTER_PACKAGE_STRING "1.0.0 mcrouter"
#endif

}}} // facebook::memcache::mcrouter
