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

namespace facebook { namespace memcache {

template<class Operation, class Request>
bool fbTraceOnSend(Operation, const McRequest& request, const AccessPoint& ap);

template<class Operation, class Reply>
void fbTraceOnReceive(Operation, const mc_fbtrace_info_s* fbtraceInfo,
                      const Reply& reply);

}}  // facebook::memcache

#include "FBTrace-inl.h"
