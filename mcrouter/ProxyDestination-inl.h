/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/config-impl.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestinationMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <int Op, class Request>
typename ReplyType<McOperation<Op>, Request>::type
ProxyDestination::send(const Request& request, McOperation<Op>,
                       DestinationRequestCtx& req_ctx,
                       std::chrono::milliseconds timeout) {
  proxy->destinationMap->markAsActive(*this);
  auto reply = getAsyncMcClient().sendSync(request, McOperation<Op>(), timeout);
  onReply(reply, req_ctx);
  return reply;
}

}}}  // facebook::memcache::mcrouter
