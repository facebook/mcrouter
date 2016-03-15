/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TypedToMcRequestConverter.h"

namespace facebook {
namespace memcache {

McRequestWithMcOp<mc_op_touch>
convertToMcRequest(TypedThriftRequest<cpp2::McTouchRequest>&& treq) {
  McRequestWithMcOp<mc_op_touch> req;
  req.setKey(std::move(treq->key));
  if (treq->__isset.exptime) {
    req.setExptime(treq->exptime);
  }
  return req;
}
} // memcache
} // facebook
