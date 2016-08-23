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

#include "mcrouter/lib/network/gen/MemcacheCarbon.h"
#include "mcrouter/lib/OperationTraits.h"

namespace facebook { namespace memcache {

// GetLike
template <>
struct GetLike<McGetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McGetsRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McMetagetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct GetLike<McLeaseGetRequest> {
  static const bool value = true;
  typedef void* Type;
};

// UpdateLike
template <>
struct UpdateLike<McSetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McAddRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McReplaceRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McLeaseSetRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McAppendRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McPrependRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct UpdateLike<McCasRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct DeleteLike<McDeleteRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<McIncrRequest> {
  static const bool value = true;
  typedef void* Type;
};

template <>
struct ArithmeticLike<McDecrRequest> {
  static const bool value = true;
  typedef void* Type;
};

}} // facebook::memcache
