/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <int Op>
int DestinationClient::send(const McRequest& request, McOperation<Op>,
                            void* req_ctx, uint64_t senderId) {
  auto& client = getAsyncMcClient();
  mc_op_t op = (mc_op_t)Op;
  auto pdstn = pdstn_;
  client.send(request, McOperation<Op>(),
    [op, req_ctx, pdstn] (McReply&& reply) {
      onReply(std::move(reply), op, req_ctx, pdstn);
    });

  return 0;
}

}}}  // facebook::memcache::mcrouter
