/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <limits>
#include <random>

#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/network/AsyncMcClient.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class Request>
ReplyT<Request> ProxyDestination::send(
    const Request& request,
    DestinationRequestCtx& requestContext,
    std::chrono::milliseconds timeout,
    RpcStatsContext& rpcStatsContext) {
  proxy.destinationMap()->markAsActive(*this);
  auto reply = getAsyncMcClient().sendSync(request, timeout, &rpcStatsContext);
  onReply(
      reply.result(), requestContext, rpcStatsContext, request.isBufferDirty());
  return reply;
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
