/*
 *  Copyright (c) 2015, Facebook, Inc.
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
class TypedThriftMessage;

/**
 * The following convertToTyped() methods convert McReply,
 * to the corresponding Typed Message structs
 */
TypedThriftMessage<cpp2::McGetReply> convertToTyped(McReply&& reply,
                                                    McOperation<mc_op_get>);

TypedThriftMessage<cpp2::McSetReply> convertToTyped(McReply&& reply,
                                                    McOperation<mc_op_set>);

TypedThriftMessage<cpp2::McDeleteReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_delete>);

TypedThriftMessage<cpp2::McTouchReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_touch>);

TypedThriftMessage<cpp2::McLeaseGetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_lease_get>);

TypedThriftMessage<cpp2::McLeaseSetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_lease_set>);

TypedThriftMessage<cpp2::McAddReply> convertToTyped(McReply&& reply,
                                                    McOperation<mc_op_add>);

TypedThriftMessage<cpp2::McReplaceReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_replace>);

TypedThriftMessage<cpp2::McGetsReply> convertToTyped(McReply&& reply,
                                                     McOperation<mc_op_gets>);

TypedThriftMessage<cpp2::McCasReply> convertToTyped(McReply&& reply,
                                                    McOperation<mc_op_cas>);

TypedThriftMessage<cpp2::McIncrReply> convertToTyped(McReply&& reply,
                                                     McOperation<mc_op_incr>);

TypedThriftMessage<cpp2::McDecrReply> convertToTyped(McReply&& reply,
                                                     McOperation<mc_op_decr>);

TypedThriftMessage<cpp2::McMetagetReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_metaget>);

TypedThriftMessage<cpp2::McAppendReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_append>);

TypedThriftMessage<cpp2::McPrependReply> convertToTyped(
    McReply&& reply, McOperation<mc_op_prepend>);
} // memcache
} // facebook
