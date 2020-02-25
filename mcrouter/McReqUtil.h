/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "mcrouter/config.h"

// Request deadline related helper functions
template <typename T, typename = std::void_t<>>
constexpr auto hasConstDeadlineMs = false;
template <typename T>
constexpr auto hasConstDeadlineMs<
    T,
    std::void_t<decltype(std::declval<const T&>().deadlineMs())>> = true;

template <typename T, typename = std::void_t<>>
constexpr auto hasNonConstDeadlineMs = false;
template <typename T>
constexpr auto hasNonConstDeadlineMs<
    T,
    std::void_t<decltype(std::declval<std::decay_t<T>&>().deadlineMs())>> =
    true;

/**
 * setRequestDeadline - sets request deadline time to current time + deadlineMs
 *                     Applicable only to the Request types that support
 *                     request deadlines by having deadlineMs field in them
 *                    If the request does not have deadlineMs field, this API
 *                    is a no-op
 */
template <class Request>
void setRequestDeadline(Request& req, uint64_t deadlineMs) {
  if constexpr (hasNonConstDeadlineMs<Request>) {
    req.deadlineMs() =
        facebook::memcache::mcrouter::getCurrentTimeInMs() + deadlineMs;
  }
}

/**
 * isRequestDeadlineExceeded - checks if the request deadline exceeded by
 *                    comparing the deadline time with current time.
 *                    If the request has not set the deadline time, then this
 *                    API returns false.
 *                    Applicable only to the Request types that support
 *                    request deadlines by having deadlineMs field in them
 *                    If the request does not have deadlineMs field, this API
 *                    always return false.
 */
template <class Request>
bool isRequestDeadlineExceeded(const Request& req) {
  if constexpr (hasConstDeadlineMs<Request>) {
    if (req.deadlineMs() > 0) {
      return facebook::memcache::mcrouter::getCurrentTimeInMs() >
          req.deadlineMs();
    }
  }
  return false;
}

/**
 * getDeadline - returns a pair with
 *    a bool - indicating if the request has support for deadline
 *    a uint64_t indicating the absolute deadline time in milliseconds since
 *               epoch
 */
template <class Request>
std::pair<bool, uint64_t> getDeadline(const Request& req) {
  if constexpr (hasConstDeadlineMs<Request>) {
    return {true, req.deadlineMs()};
  }
  return {false, 0};
}

/**
 * getRemainingTime - returns a pair with
 *    a bool - indicating if the request has support for deadline
 *    a uint64_t indicating the remaining time in milliseconds since epoch
 */
template <class Request>
std::pair<bool, uint64_t> getRemainingTime(const Request& req) {
  if constexpr (hasConstDeadlineMs<Request>) {
    auto deadlineMs = req.deadlineMs();
    auto currentTime = facebook::memcache::mcrouter::getCurrentTimeInMs();
    if (deadlineMs() > 0 && (deadlineMs > currentTime)) {
      return {true, deadlineMs - currentTime};
    }
    return {true, 0};
  }
  return {false, 0};
}
