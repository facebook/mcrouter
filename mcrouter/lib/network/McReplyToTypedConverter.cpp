/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McReplyToTypedConverter.h"

#include <arpa/inet.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

namespace facebook {
namespace memcache {

namespace {

/**
 * Fills the result for the reply.
 * If its an error reply, fills up the error message and returns false
 */
template <class ThriftType>
bool fillResult(const McReply& reply, TypedThriftReply<ThriftType>& tres) {
  tres->result = reply.result();
  if (reply.isError()) {
    if (reply.hasValue()) {
      tres->__isset.message = true;
      tres->message = reply.valueRangeSlow().str();
    }
    return false;
  }
  return true;
}

template <class GetType>
void getLikeCommon(McReply&& reply, TypedThriftReply<GetType>& tres) {
  if (!fillResult(reply, tres)) {
    return;
  }

  if (reply.hasValue()) {
    tres->__isset.value = true;
    tres->value = reply.value();
    if (reply.flags() != 0) {
      tres->__isset.flags = true;
      tres->flags = reply.flags();
    }
  }
}

template <class UpdateType>
void updateLikeCommon(McReply&& reply, TypedThriftReply<UpdateType>& tres) {
  fillResult(reply, tres);
}

template <class ArithType>
void arithmeticLikeCommon(McReply&& reply, TypedThriftReply<ArithType>& tres) {
  if (!fillResult(reply, tres)) {
    return;
  }
  tres->__isset.delta = true;
  tres->delta = reply.delta();
}

} // anonymous

TypedThriftReply<cpp2::McGetReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_get>) {
  TypedThriftReply<cpp2::McGetReply> tres;
  getLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McSetReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_set>) {
  TypedThriftReply<cpp2::McSetReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McDeleteReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_delete>) {
  TypedThriftReply<cpp2::McDeleteReply> tres;
  fillResult(reply, tres);
  return tres;
}

TypedThriftReply<cpp2::McTouchReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_touch>) {
  TypedThriftReply<cpp2::McTouchReply> tres;
  fillResult(reply, tres);
  return tres;
}

TypedThriftReply<cpp2::McLeaseGetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_lease_get>) {
  TypedThriftReply<cpp2::McLeaseGetReply> tres;
  if (!reply.isError()) {
    tres->__isset.leaseToken = true;
    tres->leaseToken = reply.leaseToken();
  }
  getLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McLeaseSetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_lease_set>) {
  TypedThriftReply<cpp2::McLeaseSetReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McAddReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_add>) {
  TypedThriftReply<cpp2::McAddReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McReplaceReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_replace>) {
  TypedThriftReply<cpp2::McReplaceReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McGetsReply> convertToTyped(McReply&& reply,
                                                   McOperation<mc_op_gets>) {
  TypedThriftReply<cpp2::McGetsReply> tres;
  if (!reply.isError()) {
    tres->__isset.casToken = true;
    tres->casToken = reply.cas();
  }
  getLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McCasReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_cas>) {
  TypedThriftReply<cpp2::McCasReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McIncrReply> convertToTyped(McReply&& reply,
                                                   McOperation<mc_op_incr>) {
  TypedThriftReply<cpp2::McIncrReply> tres;
  arithmeticLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McDecrReply> convertToTyped(McReply&& reply,
                                                   McOperation<mc_op_decr>) {
  TypedThriftReply<cpp2::McDecrReply> tres;
  arithmeticLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McMetagetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_metaget>) {
  TypedThriftReply<cpp2::McMetagetReply> tres;
  if (!fillResult(reply, tres)) {
    return tres;
  }
  tres->__isset.age = true;
  tres->age = reply.number();
  tres->__isset.exptime = true;
  tres->exptime = reply.exptime();
  char ipStr[INET6_ADDRSTRLEN];

  if (reply.ipv() != 0) {
    int af = (reply.ipv() == 6) ? AF_INET6 : AF_INET;
    auto ret = inet_ntop(af,
                         reinterpret_cast<const void*>(&reply.ipAddress()),
                         ipStr,
                         INET6_ADDRSTRLEN);
    if (ret != nullptr) {
      tres->__isset.ipAddress = true;
      tres->ipAddress = ipStr;
      tres->__isset.ipv = true;
      tres->ipv = reply.ipv();
    }
  }
  return tres;
}

TypedThriftReply<cpp2::McAppendReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_append>) {
  TypedThriftReply<cpp2::McAppendReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

TypedThriftReply<cpp2::McPrependReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_prepend>) {
  TypedThriftReply<cpp2::McPrependReply> tres;
  updateLikeCommon(std::move(reply), tres);
  return tres;
}

} // memcache
} // facebook
