/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/routes/McOpList.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <int Op, class Request>
typename ReplyType<McOperation<Op>, Request>::type
DestinationClient::send(const Request& request, McOperation<Op>,
                        uint64_t senderId, std::chrono::milliseconds timeout) {
  auto reply = getAsyncMcClient().sendSync(request, McOperation<Op>(), timeout);
  updateStats(reply.result(), (mc_op_t)Op);
  return reply;
}

}}}  // facebook::memcache::mcrouter
