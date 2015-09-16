/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McReply.h"

namespace facebook {
namespace memcache {

class CaretReplyConverter {
 public:
  void dispatchTypedRequest(size_t typeId,
                            const folly::IOBuf& bodyBuffer,
                            McReply& reply) {
    reply.setResult(mc_res_remote_error);
    reply.setValue("Not supported");
  }
};
}
} // facebook::memcache
