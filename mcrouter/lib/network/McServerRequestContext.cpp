/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McServerRequestContext.h"

#include "mcrouter/lib/network/McServerTransaction.h"

namespace facebook { namespace memcache {

void McServerRequestContext::sendReply(McReply&& reply) {
  transaction_.sendReply(std::move(reply));
}

}}  // facebook::memcache
