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

template <class M>
class TypedThriftRequest;

// GetLike
template <>
struct GetLike<TypedThriftRequest<cpp2::McGetRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<TypedThriftRequest<cpp2::McGetsRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<TypedThriftRequest<cpp2::McMetagetRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<TypedThriftRequest<cpp2::McLeaseGetRequest>> {
  static const bool value = true;
  typedef void* Type;
};

// UpdateLike
template <>
struct UpdateLike<TypedThriftRequest<cpp2::McSetRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<TypedThriftRequest<cpp2::McAddRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<TypedThriftRequest<cpp2::McReplaceRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<TypedThriftRequest<cpp2::McLeaseSetRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<TypedThriftRequest<cpp2::McAppendRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<TypedThriftRequest<cpp2::McPrependRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<TypedThriftRequest<cpp2::McCasRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct DeleteLike<TypedThriftRequest<cpp2::McDeleteRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<TypedThriftRequest<cpp2::McIncrRequest>> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<TypedThriftRequest<cpp2::McDecrRequest>> {
  static const bool value = true;
  typedef void* Type;
};

}} // facebook::memcache
