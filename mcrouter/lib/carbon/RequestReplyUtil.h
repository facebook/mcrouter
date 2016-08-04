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

#include <folly/Range.h>

namespace folly {
class IOBuf;
}

namespace carbon {

template <class R>
typename std::enable_if<R::hasValue, const folly::IOBuf*>::type
valuePtrUnsafe(const R& requestOrReply) {
  return &requestOrReply.value();
}
template <class R>
typename std::enable_if<R::hasValue, folly::IOBuf*>::type
valuePtrUnsafe(R& requestOrReply) {
  return &requestOrReply.value();
}
template <class R>
typename std::enable_if<!R::hasValue, folly::IOBuf*>::type
valuePtrUnsafe(const R& requestOrReply) {
  return nullptr;
}

template <class R>
typename std::enable_if<R::hasValue, folly::StringPiece>::type
valueRangeSlow(R& requestOrReply) {
  requestOrReply.value().coalesce();
  return folly::StringPiece(
      reinterpret_cast<const char*>(requestOrReply.value().data()),
      requestOrReply.value().length());
}
template <class R>
typename std::enable_if<!R::hasValue, folly::StringPiece>::type
valueRangeSlow(R& requestOrReply) {
  return folly::StringPiece();
}

} // carbon
