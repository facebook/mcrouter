/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

template <class OnRequest>
void McServerOnRequestWrapper<OnRequest>::requestReady(
  McServerRequestContext&& ctx, McRequest&& req, mc_op_t operation) {

  switch (operation) {
#define MC_OP(MC_OPERATION)                                             \
    case MC_OPERATION::mc_op:                                           \
      onRequest_.onRequest(std::move(ctx), std::move(req),              \
                           MC_OPERATION());                             \
      break;
#include "mcrouter/lib/McOpList.h"

    case mc_op_unknown:
    case mc_op_servererr:
    case mc_nops:
      CHECK(false) << "internal operation type passed to requestReady()";
      break;
  }
}

}}  // facebook::memcache
