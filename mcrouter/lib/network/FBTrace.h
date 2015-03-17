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

#include <folly/Range.h>

#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/AccessPoint.h"

// Forward declare struct, we wouldn't use it if it wasn't enabled.
struct mc_fbtrace_info_s;
typedef mc_fbtrace_info_s mc_fbtrace_info_t;

namespace facebook { namespace memcache {

/**
 * Class that uses SFINAE to check if Request type provides fbtraceInfo method.
 */
template <class Request>
class RequestHasFbTraceInfo {
  template <class T> static char check(decltype(&T::fbtraceInfo));
  template <class T> static int check(...);
 public:
  static constexpr bool value = sizeof(check<Request>(0)) == sizeof(char);
};

template<class Operation, class Request>
bool fbTraceOnSend(Operation, const McRequest& request, const AccessPoint& ap);

template<class Operation, class Reply>
void fbTraceOnReceive(Operation, const mc_fbtrace_info_s* fbtraceInfo,
                      const Reply& reply);

}}  // facebook::memcache

#include "FBTrace-inl.h"
