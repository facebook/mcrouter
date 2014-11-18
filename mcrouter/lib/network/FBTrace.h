#pragma once

#include <folly/Range.h>

#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McReply.h"

namespace facebook { namespace memcache {

template<class Operation, class Request>
bool fbTraceOnSend(Operation, const McRequest& request, const AccessPoint& ap);

template<class Operation, class Request, class Reply>
void fbTraceOnReceive(Operation, const Request& request, const Reply& reply);

}}  // facebook::memcache

#include "FBTrace-inl.h"
