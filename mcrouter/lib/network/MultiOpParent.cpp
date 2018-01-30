/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MultiOpParent.h"

namespace facebook {
namespace memcache {

MultiOpParent::MultiOpParent(McServerSession& session, uint64_t blockReqid)
    : session_(session), block_(session, blockReqid, true /* noReply */) {}

bool MultiOpParent::reply(
    mc_res_t result,
    uint32_t errorCode,
    std::string&& errorMessage) {
  bool stolen = false;
  // If not a hit or a miss, and we didn't store a reply yet,
  // take ownership of the error reply and tell caller not to reply
  if (!(result == mc_res_found || result == mc_res_notfound) &&
      !reply_.hasValue()) {
    stolen = true;
    error_ = true;
    reply_.emplace(result);
    reply_->message() = std::move(errorMessage);
    reply_->appSpecificErrorCode() = errorCode;
  }

  assert(waiting_ > 0);
  --waiting_;

  if (!waiting_ && end_.hasValue()) {
    release();
  }

  return stolen;
}

void MultiOpParent::recordEnd(uint64_t reqid) {
  end_ = McServerRequestContext(
      session_,
      reqid,
      false /* noReply */,
      nullptr /* multiOpParent */,
      true /* isEndContext */);
  if (!waiting_) {
    release();
  }
}

void MultiOpParent::release() {
  if (!reply_.hasValue()) {
    reply_.emplace(mc_res_found);
  }
  McServerRequestContext::reply(std::move(*end_), std::move(*reply_));
  // It doesn't really matter what reply type we use for the multi-op
  // blocking context
  McServerRequestContext::reply(std::move(block_), McGetReply());
}
}
} // facebook::memcache
