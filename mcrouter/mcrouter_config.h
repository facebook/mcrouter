/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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

namespace facebook { namespace memcache {

class McReplyBase;
class McrouterOptions;

namespace mcrouter {

class ConfigApi;
class DestinationClient;
class ExtraRouteHandleProviderIf;
class LoggingProxyRequestContext;
class mcrouter_t;
class McrouterLogger;
class proxy_t;
class TkoLog;

typedef DestinationClient DestinationMcClient;
typedef LoggingProxyRequestContext GenericProxyRequestContext;
struct ProxyStatsContainer {
  explicit ProxyStatsContainer(proxy_t*) {}
};
struct RouterLogger {
};

/**
 * Implementation of unordered_map with string keys that accepts StringPiece
 * as arguments. It may be implemented more efficiently when needed. Also
 * it should be totally redundant in C++14.
 */
template <class Value>
class StringKeyedUnorderedMap : public std::unordered_map<std::string, Value> {
 private:
  using Base = std::unordered_map<std::string, Value>;
 public:
  using iterator = typename Base::iterator;
  using const_iterator = typename Base::const_iterator;
  using size_type = typename Base::size_type;
  using value_type = typename Base::value_type;
  using mapped_type = typename Base::mapped_type;

  explicit StringKeyedUnorderedMap(size_type n = 0)
      : Base(n) { }

  StringKeyedUnorderedMap(const StringKeyedUnorderedMap& other)
      : Base(other) { }

  StringKeyedUnorderedMap(StringKeyedUnorderedMap&& other) noexcept
      : Base(std::move(other)) {
  }

  // no need for copy/move overload as StringPiece is small struct
  mapped_type& operator[](folly::StringPiece key) {
    return Base::operator[](key.str());
  }

  mapped_type& at(folly::StringPiece key) {
    return Base::at(key.str());
  }

  const mapped_type& at(folly::StringPiece key) const {
    return Base::at(key.str());
  }

  iterator find(folly::StringPiece key) {
    return Base::find(key.str());
  }

  const_iterator find(folly::StringPiece key) const {
    return Base::find(key.str());
  }

  template <class... Args>
  std::pair<iterator, bool> emplace(folly::StringPiece key, Args&&... args) {
    return Base::emplace(key.str(), std::forward<Args>(args)...);
  }

  std::pair<iterator, bool> insert(std::pair<folly::StringPiece, Value> val) {
    return Base::insert(std::make_pair(val.first.str(), std::move(val.second)));
  }

  size_type erase(folly::StringPiece key) {
    return Base::erase(key.str());
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

int router_configure_from_string(mcrouter_t* router, folly::StringPiece input);

bool read_standalone_flavor(
    const std::string& flavor,
    std::unordered_map<std::string, std::string>& option_dict,
    std::unordered_map<std::string, std::string>& st_option_dict);

std::unique_ptr<ConfigApi> createConfigApi(const McrouterOptions& opts);

std::string performOptionSubstitution(std::string str);

bool standaloneInit(const McrouterOptions& opts);

bool preprocessGetErrors(const McrouterOptions& opts, McReplyBase& reply);

std::unique_ptr<ExtraRouteHandleProviderIf> createExtraRouteHandleProvider();

std::unique_ptr<McrouterLogger> createMcrouterLogger(mcrouter_t* router);

inline bool mcrouterLoopOnce(folly::EventBase* eventBase) {
  return eventBase->loopOnce();
}

McrouterOptions defaultTestOptions();

std::vector<std::string> defaultTestCommandLineArgs();

std::shared_ptr<RouterLogger> createRouterLogger();

void logTkoEvent(proxy_t* proxy, const TkoLog& tkoLog);

void initFailureLogger();

#ifdef PACKAGE_STRING
  #define MCROUTER_PACKAGE_STRING PACKAGE_STRING
#else
  #define MCROUTER_PACKAGE_STRING "mcrouter 1.0"
#endif

}}} // facebook::memcache::mcrouter
