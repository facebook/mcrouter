/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/config-impl.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestinationMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <int Op, class Request>
typename ReplyType<McOperation<Op>, Request>::type
ProxyDestination::send(const Request& request, McOperation<Op>,
                       DestinationRequestCtx& req_ctx, uint64_t senderId) {
  FBI_ASSERT(proxy->magic == proxy_magic);

  proxy->destinationMap->markAsActive(*this);
  auto reply = client_->send(request, McOperation<Op>(), senderId);
  onReply(reply, req_ctx);
  return reply;
}

}}}  // facebook::memcache::mcrouter
