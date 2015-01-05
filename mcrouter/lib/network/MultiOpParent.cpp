/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MultiOpParent.h"

namespace facebook { namespace memcache {

MultiOpParent::MultiOpParent(McServerSession& session, uint64_t blockReqid)
    : session_(session),
      block_(session, mc_op_get, blockReqid, true, nullptr) {
}

bool MultiOpParent::reply(McReply&& r) {
  bool stole = false;
  /* If not a hit or a miss, and we didn't store a reply yet,
     steal it */
  if (!(r.result() == mc_res_found ||
        r.result() == mc_res_notfound) &&
      (!reply_.hasValue() ||
       r.worseThan(reply_.value()))) {
    stole = true;
    error_ = true;
    reply_ = std::move(r);
  }

  assert(waiting_ > 0);
  --waiting_;

  if (!waiting_ && end_.hasValue()) {
    release();
  }

  return stole;
}

void MultiOpParent::recordEnd(uint64_t reqid) {
  end_ = McServerRequestContext(session_, mc_op_end, reqid);
  if (!waiting_) {
    release();
  }
}

void MultiOpParent::release() {
  if (!reply_.hasValue()) {
    reply_ = McReply(mc_res_found);
  }
  McServerRequestContext::reply(std::move(*end_), std::move(*reply_));
  McServerRequestContext::reply(std::move(block_), McReply());
}

}}  // facebook::memcache
