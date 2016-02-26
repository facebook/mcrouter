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

#include <folly/Range.h>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/network/ThriftMessageList.h"

namespace facebook {
namespace memcache {

class McReply;

template <class T>
class TypedThriftReply;

/**
 * The following convertToTyped() methods convert McReply,
 * to the corresponding Typed Message structs
 */
TypedThriftReply<cpp2::McGetReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_get>);

TypedThriftReply<cpp2::McSetReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_set>);

TypedThriftReply<cpp2::McDeleteReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_delete>);

TypedThriftReply<cpp2::McTouchReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_touch>);

TypedThriftReply<cpp2::McLeaseGetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_lease_get>);

TypedThriftReply<cpp2::McLeaseSetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_lease_set>);

TypedThriftReply<cpp2::McAddReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_add>);

TypedThriftReply<cpp2::McReplaceReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_replace>);

TypedThriftReply<cpp2::McGetsReply> convertToTyped(McReply&& reply,
                                                   McOperation<mc_op_gets>);

TypedThriftReply<cpp2::McCasReply> convertToTyped(McReply&& reply,
                                                  McOperation<mc_op_cas>);

TypedThriftReply<cpp2::McIncrReply> convertToTyped(McReply&& reply,
                                                   McOperation<mc_op_incr>);

TypedThriftReply<cpp2::McDecrReply> convertToTyped(McReply&& reply,
                                                   McOperation<mc_op_decr>);

TypedThriftReply<cpp2::McMetagetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_metaget>);

TypedThriftReply<cpp2::McAppendReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_append>);

TypedThriftReply<cpp2::McPrependReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_prepend>);
} // memcache
} // facebook
