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

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/OperationTraits.h"

namespace facebook { namespace memcache {

/*
 * GetLike<> et al. for Typed Thrift requests.
 */
template <>
struct GetLike<cpp2::McGetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<cpp2::McGetsRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<cpp2::McMetagetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<cpp2::McLeaseGetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McSetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McAddRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McReplaceRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McLeaseSetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McAppendRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McPrependRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<cpp2::McCasRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct DeleteLike<cpp2::McDeleteRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<cpp2::McIncrRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<cpp2::McDecrRequest> {
  static const bool value = true;
  typedef void* Type;
};

}}
