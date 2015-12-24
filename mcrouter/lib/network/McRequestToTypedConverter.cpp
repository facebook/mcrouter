/*
 *  Copyright (c) 2015, Facebook, Inc.
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

template <class GetType>
void getLikeCommon(TypedThriftMessage<GetType>& treq, const McRequest& req) {
  treq->key = req.key().clone();
}

template <class UpdateType>
void updateLikeCommon(TypedThriftMessage<UpdateType>& treq,
                      const McRequest& req) {
  treq->key = req.key().clone();
  treq->value = req.value().clone();
  treq->exptime = req.exptime();
  treq->flags = req.flags();
}

template <class ArithType>
void arithmeticLikeCommon(TypedThriftMessage<ArithType>& treq,
                          const McRequest& req) {
  treq->key = req.key().clone();
  treq->delta = req.delta();
}

} // anoymous

TypedThriftMessage<cpp2::McGetRequest> convertToTyped(const McRequest& req,
                                                      McOperation<mc_op_get>) {
  TypedThriftMessage<cpp2::McGetRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McSetRequest> convertToTyped(const McRequest& req,
                                                      McOperation<mc_op_set>) {
  TypedThriftMessage<cpp2::McSetRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McDeleteRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_delete>) {
  TypedThriftMessage<cpp2::McDeleteRequest> treq;
  treq->key = req.key().clone();
  if (req.exptime() != 0) {
    treq->__isset.exptime = true;
    treq->exptime = req.exptime();
  }
  return treq;
}

TypedThriftMessage<cpp2::McTouchRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_touch>) {
  TypedThriftMessage<cpp2::McTouchRequest> treq;
  treq->key = req.key().clone();
  if (req.exptime() != 0) {
    treq->__isset.exptime = true;
    treq->exptime = req.exptime();
  }
  return treq;
}

TypedThriftMessage<cpp2::McLeaseGetRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_lease_get>) {
  TypedThriftMessage<cpp2::McLeaseGetRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McLeaseSetRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_lease_set>) {
  TypedThriftMessage<cpp2::McLeaseSetRequest> treq;
  treq->leaseToken = req.leaseToken();
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McAddRequest> convertToTyped(const McRequest& req,
                                                      McOperation<mc_op_add>) {
  TypedThriftMessage<cpp2::McAddRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McReplaceRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_replace>) {
  TypedThriftMessage<cpp2::McReplaceRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McGetsRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_gets>) {
  TypedThriftMessage<cpp2::McGetsRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McCasRequest> convertToTyped(const McRequest& req,
                                                      McOperation<mc_op_cas>) {
  TypedThriftMessage<cpp2::McCasRequest> treq;
  treq->casToken = req.cas();
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McIncrRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_incr>) {
  TypedThriftMessage<cpp2::McIncrRequest> treq;
  arithmeticLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McDecrRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_decr>) {
  TypedThriftMessage<cpp2::McDecrRequest> treq;
  arithmeticLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McMetagetRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_metaget>) {
  TypedThriftMessage<cpp2::McMetagetRequest> treq;
  getLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McVersionRequest> convertToTyped(
    const McRequest&, McOperation<mc_op_version>) {
  return TypedThriftMessage<cpp2::McVersionRequest>();
}

TypedThriftMessage<cpp2::McAppendRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_append>) {
  TypedThriftMessage<cpp2::McAppendRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}

TypedThriftMessage<cpp2::McPrependRequest> convertToTyped(
    const McRequest& req, McOperation<mc_op_prepend>) {
  TypedThriftMessage<cpp2::McPrependRequest> treq;
  updateLikeCommon(treq, req);
  return treq;
}
} // memcache
} // facebook
