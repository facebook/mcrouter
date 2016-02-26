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

#include <cassert>
#include <string>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>

#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/network/RawThriftMessageTraits.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

template <int op>
uint32_t exptime(const McRequestWithMcOp<op>& request) {
  return request.exptime();
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasExptime, uint32_t>::type
exptime(const TypedThriftRequest<M>& request) {
  return request->exptime;
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasExptime, uint32_t>::type
exptime(const TypedThriftRequest<M>& request) {
  return 0;
}

template <int op>
const folly::IOBuf& value(const McRequestWithMcOp<op>& request) {
  return request.value();
}

template <class M>
typename std::enable_if<RequestTraits<M>::hasValue, const folly::IOBuf&>::type
value(const TypedThriftRequest<M>& request) {
  return request->value;
}

template <class M>
typename std::enable_if<!RequestTraits<M>::hasValue, folly::IOBuf>::type
value(const TypedThriftRequest<M>& request) {
  LOG(DFATAL) << "value() called on Thrift message of type "
    << typeid(M).name();
  return folly::IOBuf(folly::IOBuf::COPY_BUFFER, "");
}

}} // facebook::memcache
