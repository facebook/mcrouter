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

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/OperationTraits.h"

namespace facebook { namespace memcache {

/*
 * GetLike, DeleteLike, etc. specializations for McOperation types.
 *
 * Note: the use of GetLike<McOperation<...>> and friends is deprecated.
 * New code should prefer GetLike<RequestType>, where RequestType is a
 * specialization of McRequestWithOp or TypedThriftRequest.
 * See also mcrouter/lib/network/ThriftMessageTraits.h.
 */
template <>
struct GetLike<McOperation<mc_op_get>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McOperation<mc_op_gets>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McOperation<mc_op_metaget>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McOperation<mc_op_lease_get>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_set>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_add>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_replace>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_lease_set>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_append>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_prepend>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McOperation<mc_op_cas>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct DeleteLike<McOperation<mc_op_delete>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<McOperation<mc_op_incr>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<McOperation<mc_op_decr>> {
  static const bool value = true;
  typedef void* Type;
};

/*
 * GetLike et al. for McRequestWithOp requests.
 *
 * TODO(jmswen) Eventually all the GetLike<McOperation<...>> specializations
 * should be replaced by their GetLike<McRequestWithOp<...>> counterparts.
 */
template <>
struct GetLike<McRequestWithMcOp<mc_op_get>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McRequestWithMcOp<mc_op_gets>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McRequestWithMcOp<mc_op_metaget>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McRequestWithMcOp<mc_op_lease_get>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_set>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_add>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_replace>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_lease_set>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_append>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_prepend>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McRequestWithMcOp<mc_op_cas>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct DeleteLike<McRequestWithMcOp<mc_op_delete>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<McRequestWithMcOp<mc_op_incr>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<McRequestWithMcOp<mc_op_decr>> {
  static const bool value = true;
  typedef void* Type;
};

}} // facebook::memcache
