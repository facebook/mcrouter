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

template <class M>
struct ReplyTraits;

/**
 * Request traits
 */
template <>
struct RequestTraits<cpp2::McGetRequest> {
  static constexpr const char* name = "get";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McSetRequest> {
  static constexpr const char* name = "set";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McDeleteRequest> {
  static constexpr const char* name = "delete";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McLeaseGetRequest> {
  static constexpr const char* name = "lease-get";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McLeaseSetRequest> {
  static constexpr const char* name = "lease-set";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McAddRequest> {
  static constexpr const char* name = "add";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McReplaceRequest> {
  static constexpr const char* name = "replace";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McGetsRequest> {
  static constexpr const char* name = "gets";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McCasRequest> {
  static constexpr const char* name = "cas";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McIncrRequest> {
  static constexpr const char* name = "incr";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McDecrRequest> {
  static constexpr const char* name = "decr";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McMetagetRequest> {
  static constexpr const char* name = "metaget";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McVersionRequest> {
  static constexpr const char* name = "version";
  static constexpr bool hasKey = false;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McAppendRequest> {
  static constexpr const char* name = "append";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McPrependRequest> {
  static constexpr const char* name = "prepend";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct RequestTraits<cpp2::McTouchRequest> {
  static constexpr const char* name = "touch";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = true;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McShutdownRequest> {
  static constexpr const char* name = "shutdown";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McQuitRequest> {
  static constexpr const char* name = "quit";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McStatsRequest> {
  static constexpr const char* name = "stats";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McExecRequest> {
  static constexpr const char* name = "exec";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McFlushReRequest> {
  static constexpr const char* name = "flushre";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct RequestTraits<cpp2::McFlushAllRequest> {
  static constexpr const char* name = "flushall";
  static constexpr bool hasKey = true;
  static constexpr bool hasExptime = false;
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

/**
 * Reply traits
 */
template <>
struct ReplyTraits<cpp2::McGetReply> {
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct ReplyTraits<cpp2::McSetReply> {
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct ReplyTraits<cpp2::McDeleteReply> {
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct ReplyTraits<cpp2::McLeaseGetReply> {
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct ReplyTraits<cpp2::McLeaseSetReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McAddReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McReplaceReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McGetsReply> {
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = true;
};

template <>
struct ReplyTraits<cpp2::McCasReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McIncrReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McDecrReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McMetagetReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McVersionReply> {
  static constexpr bool hasValue = true;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McAppendReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McPrependReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McTouchReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McShutdownReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McQuitReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McStatsReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McExecReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McFlushReReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

template <>
struct ReplyTraits<cpp2::McFlushAllReply> {
  static constexpr bool hasValue = false;
  static constexpr bool hasFlags = false;
};

}} // facebook::memcache
