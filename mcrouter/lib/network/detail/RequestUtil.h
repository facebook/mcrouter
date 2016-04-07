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

#include <stdexcept>
#include <string>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/network/RawThriftMessageTraits.h"

namespace facebook { namespace memcache {

template <class M>
class TypedThriftReply;
template <class M>
class TypedThriftRequest;

namespace detail {

/**
 * Request-related helpers.
 */
template <class M>
typename std::enable_if<RequestTraits<M>::hasExptime, int32_t>::type
exptime(const TypedThriftRequest<M>& request) {
  return request->__isset.exptime ? request->exptime : 0;
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasExptime, int32_t>::type
exptime(const TypedThriftRequest<M>& request) {
  return 0;
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasExptime, void>::type
setExptime(TypedThriftRequest<M>& request, int32_t expt) {
  request->set_exptime(expt);
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasExptime, void>::type
setExptime(TypedThriftRequest<M>&, int32_t) {
  /* no-op */
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasValue, const folly::IOBuf*>::type
valuePtrUnsafe(const TypedThriftRequest<M>& request) {
  return request->__isset.value ? &request->value : nullptr;
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasValue, const folly::IOBuf*>::type
valuePtrUnsafe(const TypedThriftRequest<M>& request) {
  return nullptr;
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasValue, void>::type
setValue(TypedThriftRequest<M>& request, folly::IOBuf valueData) {
  request->set_value(std::move(valueData));
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasValue, void>::type
setValue(TypedThriftRequest<M>&, folly::IOBuf) {
  /* no-op */
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasFlags, uint64_t>::type
flags(const TypedThriftRequest<M>& request) {
  return request->__isset.flags ? request->flags : 0;
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasFlags, uint64_t>::type
flags(const TypedThriftRequest<M>&) {
  return 0;
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasFlags, void>::type
setFlags(TypedThriftRequest<M>& request, uint64_t f) {
  request->set_flags(f);
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasFlags, void>::type
setFlags(TypedThriftRequest<M>&, uint64_t) {
  /* no-op */;
}

/**
 * Reply-related helpers.
 */
template <class M>
typename std::enable_if<ReplyTraits<M>::hasValue, const folly::IOBuf*>::type
valuePtrUnsafe(const TypedThriftReply<M>& reply) {
  return reply->__isset.value ? &reply->value : nullptr;
}

template <class M>
typename std::enable_if<!ReplyTraits<M>::hasValue, const folly::IOBuf*>::type
valuePtrUnsafe(const TypedThriftReply<M>& reply) {
  return nullptr;
}

template <class M>
typename std::enable_if<ReplyTraits<M>::hasValue, void>::type
setValue(TypedThriftReply<M>& reply, folly::IOBuf valueData) {
  reply->set_value(std::move(valueData));
}

template <class M>
typename std::enable_if<!ReplyTraits<M>::hasValue, void>::type
setValue(TypedThriftReply<M>&, folly::IOBuf) {
  /* no-op */
}

template <class M>
typename std::enable_if<ReplyTraits<M>::hasFlags, uint64_t>::type
flags(const TypedThriftReply<M>& reply) {
  return reply->__isset.flags ? reply->flags : 0;
}

template <class M>
typename std::enable_if<!ReplyTraits<M>::hasFlags, uint64_t>::type
flags(const TypedThriftReply<M>&) {
  return 0;
}

template <class M>
typename std::enable_if<ReplyTraits<M>::hasFlags, void>::type
setFlags(TypedThriftReply<M>& reply, uint64_t f) {
  reply->set_flags(f);
}

template <class M>
typename std::enable_if<!ReplyTraits<M>::hasFlags, void>::type
setFlags(TypedThriftReply<M>&, uint64_t) {
  /* no-op */;
}

}}} // facebook::memcache::detail
