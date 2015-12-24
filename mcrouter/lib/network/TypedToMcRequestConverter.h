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

#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

namespace facebook {
namespace memcache {

class McRequest;

/**
 * The following convertToMcRequest() methods convert requests,
 * in the form of Typed Structs, to the corresponding McRequests
 */
template <class GetType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<GetType>&& treq,
                             Operation,
                             typename GetLike<Operation>::Type = 0);

template <class UpdateType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<UpdateType>&& treq,
                             Operation,
                             typename UpdateLike<Operation>::Type = 0);

template <class DeleteType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<DeleteType>&& treq,
                             Operation,
                             typename DeleteLike<Operation>::Type = 0);

template <class ArithType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<ArithType>&& treq,
                             Operation,
                             typename ArithmeticLike<Operation>::Type = 0);
McRequest convertToMcRequest(TypedThriftMessage<cpp2::McVersionRequest>&& treq,
                             McOperation<mc_op_version>);
McRequest convertToMcRequest(TypedThriftMessage<cpp2::McTouchRequest>&& treq,
                             McOperation<mc_op_touch>);
} // memcache
} // facebook

#include "TypedToMcRequestConverter-inl.h"
