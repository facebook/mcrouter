/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestinationMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class Request>
ReplyT<Request> ProxyDestination::send(const Request& request,
                                       DestinationRequestCtx& req_ctx,
                                       std::chrono::milliseconds timeout) {
  proxy->destinationMap->markAsActive(*this);
  auto reply = getAsyncMcClient().sendSync(request, timeout);
  onReply(reply.result(), req_ctx);
  return reply;
}

}}}  // facebook::memcache::mcrouter
