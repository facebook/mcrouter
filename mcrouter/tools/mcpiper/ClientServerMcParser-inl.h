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

namespace facebook { namespace memcache {

template <class Reply>
void ClientServerMcParser::replyReady(uint64_t reqid, mc_op_t op, Reply reply) {
  callbackFn_(reqid, reply.releasedMsg(op));
}

template <class Request>
ReplyT<Request>
ClientServerMcParser::parseReply(const UmbrellaMessageInfo& info,
                                 const uint8_t* header,
                                 const uint8_t* body,
                                 const folly::IOBuf& bodyBuffer) {
  return umbrellaParseReply<Request>(
      bodyBuffer, header, info.headerSize, body, info.bodySize);
}

}} // facebook::memcache
