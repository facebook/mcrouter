/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
namespace facebook {
namespace memcache {

namespace {
template <class UpdateType>
void updateHelper(TypedThriftRequest<UpdateType>&& treq,
                  McRequestWithMcOp<mc_op_lease_get>& req) {
  req.setLeaseToken(treq->leaseToken);
}

template <class UpdateType>
void updateHelper(TypedThriftRequest<UpdateType>&& treq,
                  McRequestWithMcOp<mc_op_cas>& req) {
  req.setCas(treq->casToken);
}

template <class UpdateType, class Operation>
void updateHelper(TypedThriftRequest<UpdateType>&& treq,
                  McRequestWithOp<Operation>& req) {
  // no-op
}
} // anonymous

template <class GetType>
McRequestWithMcOp<OpFromType<GetType, RequestOpMapping>::value>
convertToMcRequest(TypedThriftRequest<GetType>&& treq,
                   GetLikeT<TypedThriftRequest<GetType>>) {
  constexpr mc_op_t op = OpFromType<GetType, RequestOpMapping>::value;
  McRequestWithMcOp<op> req;
  req.setKey(std::move(treq->key));
  return req;
}

template <class UpdateType>
McRequestWithMcOp<OpFromType<UpdateType, RequestOpMapping>::value>
convertToMcRequest(TypedThriftRequest<UpdateType>&& treq,
                   UpdateLikeT<TypedThriftRequest<UpdateType>>) {
  constexpr mc_op_t op = OpFromType<UpdateType, RequestOpMapping>::value;
  McRequestWithMcOp<op> req;
  req.setKey(std::move(treq->key));
  req.setExptime(treq->exptime);
  req.setFlags(treq->flags);
  req.setValue(std::move(treq->value));
  updateHelper(std::move(treq), req);
  return req;
}

template <class DeleteType>
McRequestWithMcOp<OpFromType<DeleteType, RequestOpMapping>::value>
convertToMcRequest(TypedThriftRequest<DeleteType>&& treq,
                   DeleteLikeT<TypedThriftRequest<DeleteType>>) {
  constexpr mc_op_t op = OpFromType<DeleteType, RequestOpMapping>::value;
  McRequestWithMcOp<op> req;
  req.setKey(std::move(treq->key));
  if (treq->__isset.exptime) {
    req.setExptime(treq->exptime);
  }
  return req;
}

template <class ArithType>
McRequestWithMcOp<OpFromType<ArithType, RequestOpMapping>::value>
convertToMcRequest(TypedThriftRequest<ArithType>&& treq,
                   ArithmeticLikeT<TypedThriftRequest<ArithType>>) {
  constexpr mc_op_t op = OpFromType<ArithType, RequestOpMapping>::value;
  McRequestWithMcOp<op> req;
  req.setKey(std::move(treq->key));
  req.setDelta(treq->delta);
  return req;
}

inline McRequestWithMcOp<mc_op_version> convertToMcRequest(
    TypedThriftRequest<cpp2::McVersionRequest>&& treq) {
  return {};
}

} // memcache
} // facebook
