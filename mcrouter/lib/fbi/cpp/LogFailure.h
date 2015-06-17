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

#include <functional>
#include <map>
#include <string>
#include <utility>

#include <folly/Range.h>
#include <folly/Format.h>

namespace facebook { namespace memcache { namespace failure {

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

typedef std::function<void(
  folly::StringPiece file,
  int line,
  folly::StringPiece service,
  folly::StringPiece category,
  folly::StringPiece msg,
  const std::map<std::string, std::string>& contexts)>
HandlerFunc;

namespace handlers {

std::pair<std::string, HandlerFunc> verboseLogToStdError();

std::pair<std::string, HandlerFunc> logToStdError();

std::pair<std::string, HandlerFunc> throwLogicError();

}  // handlers

namespace detail {

/**
 * Log failure according to action for given category (see @setCategoryAction).
 * If no special action is provided, default constructed one will be used.
 */
void log(folly::StringPiece file,
         int line,
         folly::StringPiece service,
         folly::StringPiece category,
         folly::StringPiece msg);

/**
 * log overload to format messages automatically.
 */
template <typename... Args>
void log(folly::StringPiece file,
         int line,
         folly::StringPiece service,
         folly::StringPiece category,
         folly::StringPiece msgFormat,
         Args&&... args) {
  log(file, line, service, category,
      folly::format(msgFormat, std::forward<Args>(args)...).str());
}

}  // detail

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
 * Set debug information about your service: version, options,
 * command line args, etc.
 * Context will be logged at the end of each failure, so it is
 * simple to identify what and where is crashing.
 */
void setServiceContext(folly::StringPiece service, std::string context);

#define LOG_FAILURE(...) \
  facebook::memcache::failure::detail::log(__FILE__, __LINE__, __VA_ARGS__)

}}}  // facebook::memcache::failure
