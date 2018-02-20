/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Range.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/AccessPoint.h"

// Forward declare struct, we wouldn't use it if it wasn't enabled.
struct mc_fbtrace_info_s;
typedef mc_fbtrace_info_s mc_fbtrace_info_t;

namespace facebook {
namespace memcache {

/**
 * Class that uses SFINAE to check if Request type provides fbtraceInfo method.
 */
template <class Request>
class RequestHasFbTraceInfo {
  template <class T>
  static char check(decltype(&T::fbtraceInfo));
  template <class T>
  static int check(...);

 public:
  static constexpr bool value = sizeof(check<Request>(0)) == sizeof(char);
};

template <class Request>
bool fbTraceOnSend(const Request& request, const AccessPoint& ap);

inline void fbTraceOnReceive(
    const mc_fbtrace_info_s* fbtraceInfo,
    const mc_res_t result);

// Returns true if a rate limiting check passes and tracing can proceed.
bool traceCheckRateLimit();

// Returns the cumulative number of traces logged.
uint64_t traceGetCount();
}
} // facebook::memcache

#include "FBTrace-inl.h"
