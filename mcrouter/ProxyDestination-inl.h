/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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
    ReplyStatsContext& replyStatsContext) {
  proxy.destinationMap()->markAsActive(*this);
  auto reply =
      getAsyncMcClient().sendSync(request, timeout, &replyStatsContext);
  onReply(reply.result(), requestContext, replyStatsContext);
  return reply;
}

template <class Request>
bool ProxyDestination::shouldDrop() const {
  if (!client_) {
    return false;
  }

  auto dropProbability = client_->getDropProbability<Request>();

  if (dropProbability == 0.0) {
    return false;
  }

  return std::generate_canonical<double, std::numeric_limits<double>::digits>(
             proxy.randomGenerator()) < dropProbability;
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
