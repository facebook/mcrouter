/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

namespace facebook {
namespace memcache {

namespace {
template <class UpdateType>
void updateHelper(TypedThriftMessage<UpdateType>&& treq,
                  McRequest& req,
                  McOperation<mc_op_lease_set>) {
  req.setLeaseToken(treq->leaseToken);
}

template <class UpdateType>
void updateHelper(TypedThriftMessage<UpdateType>&& treq,
                  McRequest& req,
                  McOperation<mc_op_cas>) {
  req.setCas(treq->casToken);
}

template <class UpdateType, class Operation>
void updateHelper(TypedThriftMessage<UpdateType>&& treq,
                  McRequest& req,
                  Operation) {
  // no-op
}

} // anonymous

template <class GetType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<GetType>&& treq,
                             Operation,
                             typename GetLike<Operation>::Type) {
  McRequest req;
  req.setKey(std::move(*(treq->key)));
  return req;
}

template <class UpdateType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<UpdateType>&& treq,
                             Operation,
                             typename UpdateLike<Operation>::Type) {
  McRequest req;
  req.setKey(std::move(*(treq->key)));
  req.setExptime(treq->exptime);
  req.setFlags(treq->flags);
  req.setValue(std::move(*(treq->value)));
  updateHelper(std::move(treq), req, Operation());
  return req;
}

template <class DeleteType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<DeleteType>&& treq,
                             Operation,
                             typename DeleteLike<Operation>::Type) {
  McRequest req;
  req.setKey(std::move(*(treq->key)));
  if (treq->__isset.exptime) {
    req.setExptime(treq->exptime);
  }
  return req;
}

template <class ArithType, class Operation>
McRequest convertToMcRequest(TypedThriftMessage<ArithType>&& treq,
                             Operation,
                             typename ArithmeticLike<Operation>::Type) {
  McRequest req;
  req.setKey(std::move(*(treq->key)));
  req.setDelta(treq->delta);
  return req;
}

inline McRequest convertToMcRequest(
    TypedThriftMessage<cpp2::McVersionRequest>&& treq,
    McOperation<mc_op_version>) {
  return McRequest();
}
} // memcache
} // facebook
