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

namespace facebook { namespace memcache {

namespace detail {
struct HasKey {
  template <class M>
  static auto check(M* m) -> decltype(m->key, std::true_type());
  template <class>
  static auto check(...) -> std::false_type;
};
} // detail

template <class M>
struct ThriftMsgIsRequest : public decltype(detail::HasKey::check<M>(0)) {};

/* McVersionRequest is the only Thrift request type without a key. */
template <>
struct ThriftMsgIsRequest<cpp2::McVersionRequest> {
  static constexpr bool value = true;
};

/**
 * Type traits on the raw Thrift-generated structures. Useful in conditionally
 * defining functions, e.g., instantiate one version of the function template
 * for Thrift structures that have `key`, another version for Thrift structures
 * without a `key` field.
 */
template <class M>
struct RequestTraits;

template <>
struct RequestTraits<cpp2::McGetRequest> {
  static constexpr const char* name = "get";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McSetRequest> {
  static constexpr const char* name = "set";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McDeleteRequest> {
  static constexpr const char* name = "delete";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McLeaseGetRequest> {
  static constexpr const char* name = "lease-get";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McLeaseSetRequest> {
  static constexpr const char* name = "lease-set";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McAddRequest> {
  static constexpr const char* name = "add";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McReplaceRequest> {
  static constexpr const char* name = "replace";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McGetsRequest> {
  static constexpr const char* name = "gets";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McCasRequest> {
  static constexpr const char* name = "cas";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McIncrRequest> {
  static constexpr const char* name = "incr";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McDecrRequest> {
  static constexpr const char* name = "decr";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McMetagetRequest> {
  static constexpr const char* name = "metaget";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McVersionRequest> {
  static constexpr const char* name = "version";
  static constexpr bool hasKey = false;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
};

template <>
struct RequestTraits<cpp2::McAppendRequest> {
  static constexpr const char* name = "append";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McPrependRequest> {
  static constexpr const char* name = "prepend";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
};

template <>
struct RequestTraits<cpp2::McTouchRequest> {
  static constexpr const char* name = "touch";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = false;
};

}} // facebook::memcache
