/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>
#include <utility>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/gen/MemcacheCarbon.h"
#include "mcrouter/lib/network/CarbonMessageTraits.h"
#include "mcrouter/lib/Operation.h"

namespace facebook { namespace memcache {

/**
 * Type tags for Reply constructors.
 */
enum DefaultReplyT { DefaultReply };
enum ErrorReplyT { ErrorReply };
enum TkoReplyT { TkoReply };

template <class Request>
ReplyT<Request>
createReply(DefaultReplyT, const Request&, UpdateLikeT<Request> = 0) {
  return ReplyT<Request>(mc_res_notstored);
}

template <class Request>
ReplyT<Request> createReply(
    DefaultReplyT,
    const Request&,
    OtherThanT<Request, UpdateLike<>> = 0) {
  return ReplyT<Request>(mc_res_notfound);
}

template <class Request>
ReplyT<Request> createReply(ErrorReplyT) {
  return ReplyT<Request>(mc_res_local_error);
}

template <class Request>
ReplyT<Request> createReply(ErrorReplyT, std::string errorMessage) {
  ReplyT<Request> reply(mc_res_local_error);
  reply.message() = std::move(errorMessage);
  return reply;
}

template <class Request>
ReplyT<Request> createReply(TkoReplyT) {
  return ReplyT<Request>(mc_res_tko);
}

}}  // facebook::memcache
