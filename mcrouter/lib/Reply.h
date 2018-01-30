/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {

/**
 * Type tags for Reply constructors.
 */
enum DefaultReplyT { DefaultReply };
enum ErrorReplyT { ErrorReply };
enum TkoReplyT { TkoReply };
enum BusyReplyT { BusyReply };

template <class Request>
ReplyT<Request>
createReply(DefaultReplyT, const Request&, carbon::UpdateLikeT<Request> = 0) {
  return ReplyT<Request>(mc_res_notstored);
}

template <class Request>
ReplyT<Request> createReply(
    DefaultReplyT,
    const Request&,
    carbon::OtherThanT<Request, carbon::UpdateLike<>> = 0) {
  return ReplyT<Request>(mc_res_notfound);
}

template <class Request>
ReplyT<Request> createReply(ErrorReplyT) {
  return ReplyT<Request>(mc_res_local_error);
}

template <class Request>
ReplyT<Request> createReply(ErrorReplyT, std::string errorMessage) {
  ReplyT<Request> reply(mc_res_local_error);
  carbon::setMessageIfPresent(reply, std::move(errorMessage));
  return reply;
}

template <class Request>
ReplyT<Request>
createReply(ErrorReplyT, mc_res_t result, std::string errorMessage) {
  assert(isErrorResult(result));
  ReplyT<Request> reply(result);
  carbon::setMessageIfPresent(reply, std::move(errorMessage));
  return reply;
}

template <class Request>
ReplyT<Request> createReply(TkoReplyT) {
  return ReplyT<Request>(mc_res_tko);
}

template <class Request>
ReplyT<Request> createReply(BusyReplyT) {
  return ReplyT<Request>(mc_res_busy);
}
}
} // facebook::memcache
