/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McRequestToTypedConverter.h"

#include <folly/Range.h>

#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/OperationTraits.h"

namespace facebook {
namespace memcache {

namespace {

template <class GetType, class GetOperation>
void getLikeCommon(TypedThriftRequest<GetType>& treq,
                   const McRequestWithOp<GetOperation>& req) {
  treq->key = req.key();
}

template <class UpdateType, class UpdateOperation>
void updateLikeCommon(TypedThriftRequest<UpdateType>& treq,
                      const McRequestWithOp<UpdateOperation>& req) {
  treq->key = req.key();
  treq->value = req.value();
  treq->exptime = req.exptime();
  treq->flags = req.flags();
}

template <class ArithType, class ArithOperation>
void arithmeticLikeCommon(TypedThriftRequest<ArithType>& treq,
                          const McRequestWithOp<ArithOperation>& req) {
  treq->key = req.key();
  treq->delta = req.delta();
}

} // anonymous

TypedThriftRequest<cpp2::McGetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_get>& req) {

  TypedThriftRequest<cpp2::McGetRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McSetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_set>& req) {

  TypedThriftRequest<cpp2::McSetRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McDeleteRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_delete>& req) {

  TypedThriftRequest<cpp2::McDeleteRequest> treq;
  treq->key = req.key();
  if (req.exptime() != 0) {
    treq->__isset.exptime = true;
    treq->exptime = req.exptime();
  }
  return treq;
}

TypedThriftRequest<cpp2::McTouchRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_touch>& req) {

  TypedThriftRequest<cpp2::McTouchRequest> treq;
  treq->key = req.key();
  if (req.exptime() != 0) {
    treq->__isset.exptime = true;
    treq->exptime = req.exptime();
  }
  return treq;
}

TypedThriftRequest<cpp2::McLeaseGetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_lease_get>& req) {

  TypedThriftRequest<cpp2::McLeaseGetRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McLeaseSetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_lease_set>& req) {

  TypedThriftRequest<cpp2::McLeaseSetRequest> treq;
  treq->leaseToken = req.leaseToken();
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McAddRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_add>& req) {

  TypedThriftRequest<cpp2::McAddRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McReplaceRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_replace>& req) {

  TypedThriftRequest<cpp2::McReplaceRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McGetsRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_gets>& req) {

  TypedThriftRequest<cpp2::McGetsRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McCasRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_cas>& req) {

  TypedThriftRequest<cpp2::McCasRequest> treq;
  treq->casToken = req.cas();
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McIncrRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_incr>& req) {

  TypedThriftRequest<cpp2::McIncrRequest> treq;
  arithmeticLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McDecrRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_decr>& req) {

  TypedThriftRequest<cpp2::McDecrRequest> treq;
  arithmeticLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McMetagetRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_metaget>& req) {

  TypedThriftRequest<cpp2::McMetagetRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McVersionRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_version>&) {

  return TypedThriftRequest<cpp2::McVersionRequest>();
}

TypedThriftRequest<cpp2::McAppendRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_append>& req) {

  TypedThriftRequest<cpp2::McAppendRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftRequest<cpp2::McPrependRequest> convertToTyped(
    const McRequestWithMcOp<mc_op_prepend>& req) {

  TypedThriftRequest<cpp2::McPrependRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}
} // memcache
} // facebook
