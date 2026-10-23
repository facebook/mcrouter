/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

#include <fmt/format.h>
#include <folly/Range.h>

namespace facebook {
namespace memcache {
namespace failure {

class Category {
 public:
  /**
   * Misconfigured environment. Example: can not write to file, can not connect
   * to network, can not read SSL certs, etc.
   */
  static const char* const kBadEnvironment;
  /**
   * Invalid options (e.g. command line).
   */
  static const char* const kInvalidOption;
  /**
   * Configuration is broken.
   */
  static const char* const kInvalidConfig;
  /**
   * Dynamic configuration is broken.
   */
  static const char* const kInvalidDynamicSampler;
  /**
   * Out of memory, not enough disk space, etc.
   */
  static const char* const kOutOfResources;
  /**
   * Some invariants are broken, should be a bug.
   * Example:
   *  if (1 + 1 == 3) {
   *    log(Category::kBrokenLogic,
            "Math is broken 1 + 1 = 3, expected {}", 1 + 1);
   *  }
   */
  static const char* const kBrokenLogic;
  /**
   * System error: can not create thread, open socket, file, etc.
   */
  static const char* const kSystemError;
  /**
   * If nothing else fits, use this one (or custom string).
   */
  static const char* const kOther;

 private:
  Category() {}
};

using HandlerFunc = std::function<void(
    folly::StringPiece file,
    int line,
    folly::StringPiece service,
    folly::StringPiece category,
    folly::StringPiece msg,
    const std::map<std::string, std::string>& contexts,
    bool skipRateLimit)>;

namespace handlers {

std::pair<std::string, HandlerFunc> verboseLogToStdError();

std::pair<std::string, HandlerFunc> logToStdError();

std::pair<std::string, HandlerFunc> throwLogicError();

} // namespace handlers

namespace detail {

template <typename... Args>
constexpr bool kFirstArgIsBool = false;
template <typename First, typename... Rest>
constexpr bool kFirstArgIsBool<First, Rest...> =
    std::is_same_v<std::decay_t<First>, bool>;

/**
 * Log failure according to action for given category (see @setCategoryAction).
 * If no special action is provided, default constructed one will be used.
 *
 * @param skipRateLimit If true, bypasses rate limiting in handlers (e.g., for
 * Luna logs)
 */
void log(
    folly::StringPiece file,
    int line,
    folly::StringPiece service,
    folly::StringPiece category,
    folly::StringPiece msg,
    bool skipRateLimit = false);

/**
 * log overload to format messages automatically.
 */
template <typename... Args>
  requires(!kFirstArgIsBool<Args...>)
void log(
    folly::StringPiece file,
    int line,
    folly::StringPiece service,
    folly::StringPiece category,
    fmt::format_string<Args...> msgFormat,
    Args&&... args) {
  log(file,
      line,
      service,
      category,
      fmt::format(msgFormat, std::forward<Args>(args)...));
}

/**
 * log overload with skipRateLimit flag to format messages automatically.
 */
template <typename... Args>
void log(
    folly::StringPiece file,
    int line,
    folly::StringPiece service,
    folly::StringPiece category,
    fmt::format_string<Args...> msgFormat,
    bool skipRateLimit,
    Args&&... args) {
  log(file,
      line,
      service,
      category,
      fmt::format(msgFormat, std::forward<Args>(args)...),
      skipRateLimit);
}

} // namespace detail

/**
 * Add new failure handler. Names should be unique.
 * Before adding your own handler, consider using functions from 'handlers'
 * namespace.
 *
 * @param handler  pair { handler name, handler func }
 *
 * @return  true, if handler was added, false if the name is not unique
 */
bool addHandler(std::pair<std::string, HandlerFunc> handler);

/**
 * Add/replace failure handler.
 * Before adding your own handler, consider using functions from 'handlers'
 * namespace.
 *
 * @param handler  pair { handler name, handler func }
 *
 * @return  true, if handler was added or replaced
 */
bool setHandler(std::pair<std::string, HandlerFunc> handler);

/**
 * Remove failure handler by name.
 *
 * @param handlerName  handler to remove
 *
 * @return  true, if handler was removed
 */
bool removeHandler(folly::StringPiece handlerName);

/**
 * Set debug information about your service: version, options,
 * command line args, etc.
 * Context will be logged at the end of each failure, so it is
 * simple to identify what and where is crashing.
 */
void setServiceContext(folly::StringPiece service, std::string context);

#define LOG_FAILURE(...) \
  facebook::memcache::failure::detail::log(__FILE__, __LINE__, __VA_ARGS__)
} // namespace failure
} // namespace memcache
} // namespace facebook
