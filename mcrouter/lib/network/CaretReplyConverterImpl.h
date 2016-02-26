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

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

namespace facebook {
namespace memcache {

class CaretReplyConverter
    : public ThriftMsgDispatcher<TReplyList, CaretReplyConverter, McReply&> {

 public:
  template <class Unsupported>
  void onTypedMessage(TypedThriftReply<Unsupported>&&, McReply& reply) {
    reply.setResult(mc_res_remote_error);
  }

  void onTypedMessage(TypedThriftReply<cpp2::McGetReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McSetReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McDeleteReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McTouchReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McLeaseGetReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McLeaseSetReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McAddReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McReplaceReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McGetsReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McCasReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McIncrReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McDecrReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McMetagetReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McVersionReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McAppendReply>&& tres,
                      McReply& reply);

  void onTypedMessage(TypedThriftReply<cpp2::McPrependReply>&& tres,
                      McReply& reply);
};
} // memcache
} // facebook
