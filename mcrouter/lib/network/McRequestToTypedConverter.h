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
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

namespace facebook {
namespace memcache {

/**
 * The following convertToTypedRequest() methods,
 * convert McRequestWithOp to TypedRequest
 *
 * @param req  McRequestWithOp
 * @return TypedThriftMessage corresponding to the operation
 */
TypedThriftMessage<cpp2::McGetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_get>& req);

TypedThriftMessage<cpp2::McSetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_set>& req);

TypedThriftMessage<cpp2::McDeleteRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_delete>& req);

TypedThriftMessage<cpp2::McTouchRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_touch>& req);

TypedThriftMessage<cpp2::McLeaseGetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_lease_get>& req);

TypedThriftMessage<cpp2::McLeaseSetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_lease_set>& req);

TypedThriftMessage<cpp2::McAddRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_add>& req);

TypedThriftMessage<cpp2::McReplaceRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_replace>& req);

TypedThriftMessage<cpp2::McGetsRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_gets>& req);

TypedThriftMessage<cpp2::McCasRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_cas>& req);

TypedThriftMessage<cpp2::McIncrRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_incr>& req);

TypedThriftMessage<cpp2::McDecrRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_decr>& req);

TypedThriftMessage<cpp2::McMetagetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_metaget>& req);

TypedThriftMessage<cpp2::McVersionRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_version>& req);

TypedThriftMessage<cpp2::McAppendRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_append>& req);

TypedThriftMessage<cpp2::McPrependRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_prepend>& req);
}
}
