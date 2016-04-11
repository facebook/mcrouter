/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CaretReplyConverterImpl.h"

#include <arpa/inet.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/TypedMsg.h"

namespace facebook {
namespace memcache {

namespace {

/**
 * Fills the result for the reply and checks if the Typed Message
 * has an error result and, fills up the error message in the
 * McReply value if it does.
 * @return true if TypedMessage is not an error reply
 */
template <class ThriftType>
bool fillResult(TypedThriftReply<ThriftType>& tres, McReply& reply) {
  reply.setResult(static_cast<mc_res_t>(tres->result));
  reply.setAppSpecificErrorCode(tres.appSpecificErrorCode());
  reply.setFlags(tres.flags());
  if (mc_res_is_err(static_cast<mc_res_t>(tres->result))) {
    if (tres->__isset.message) {
      reply.setValue(std::move(tres->message));
    }
    return false;
  }
  return true;
}

template <class GetType>
void onGetCommon(TypedThriftReply<GetType>&& tres, McReply& reply) {
  if (tres->get_value()) {
    reply.setValue(std::move(tres->value));
  }
  if (!fillResult(tres, reply)) {
    return;
  }
}

template <class UpdateType>
void onUpdateCommon(TypedThriftReply<UpdateType>&& tres, McReply& reply) {
  fillResult(tres, reply);
}

template <class ArithType>
void onArithmeticCommon(TypedThriftReply<ArithType>&& tres, McReply& reply) {
  if (!fillResult(tres, reply)) {
    return;
  }
  if (!tres->__isset.delta) {
    reply.setResult(mc_res_bad_value);
    return;
  }
  reply.setDelta(tres->delta);
}
} // anonymous

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McGetReply>&& tres, McReply& reply) {
  onGetCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McSetReply>&& tres, McReply& reply) {
  if (tres->get_value()) {
    reply.setValue(std::move(tres->value));
  }
  onUpdateCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McDeleteReply>&& tres, McReply& reply) {
  if (tres->get_value()) {
    reply.setValue(std::move(tres->value));
  }
  fillResult(tres, reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McTouchReply>&& tres, McReply& reply) {
  fillResult(tres, reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McLeaseGetReply>&& tres, McReply& reply) {
  if (tres->__isset.leaseToken) {
    reply.setLeaseToken(tres->leaseToken);
  }
  onGetCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McLeaseSetReply>&& tres, McReply& reply) {
  onUpdateCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McAddReply>&& tres, McReply& reply) {
  onUpdateCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McReplaceReply>&& tres, McReply& reply) {
  onUpdateCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McGetsReply>&& tres, McReply& reply) {
  if (tres->__isset.casToken) {
    reply.setCas(tres->casToken);
  }
  onGetCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McCasReply>&& tres, McReply& reply) {
  onUpdateCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McIncrReply>&& tres, McReply& reply) {
  onArithmeticCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McDecrReply>&& tres, McReply& reply) {
  onArithmeticCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McMetagetReply>&& tres, McReply& reply) {

  if (!fillResult(tres, reply)) {
    return;
  }

  if (tres->__isset.age) {
    reply.setNumber(tres->age);
  }
  if (tres->__isset.exptime) {
    reply.setExptime(tres->exptime);
  }
  struct in6_addr addr;

  if (tres->__isset.ipAddress) {
    if (!tres->__isset.ipv) {
      reply.setResult(mc_res_bad_value);
      return;
    }
    int af = (tres->ipv == 6) ? AF_INET6 : AF_INET;
    auto ret = inet_pton(
        af, reinterpret_cast<const char*>(tres->ipAddress.data()), &addr);
    if (ret == 1) {
      reply.setIpAddress(addr, tres->ipv);
    }
  }
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McVersionReply>&& tres, McReply& reply) {
  if (!fillResult(tres, reply)) {
    return;
  }
  if (tres->__isset.value != true) {
    reply.setResult(mc_res_bad_value);
    return;
  }
  reply.setValue(std::move(tres->value));
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McAppendReply>&& tres, McReply& reply) {
  onUpdateCommon(std::move(tres), reply);
}

void CaretReplyConverter::onTypedMessage(
    TypedThriftReply<cpp2::McPrependReply>&& tres, McReply& reply) {
  onUpdateCommon(std::move(tres), reply);
}

} // memcache
} // facebook
