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

#include <type_traits>

#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>

namespace facebook {
namespace memcache {
class McServerRequestContext;
} // memcache
} // facebook

namespace carbon {

namespace detail {
inline folly::IOBuf* bufPtr(folly::Optional<folly::IOBuf>& buf) {
  return buf.get_pointer();
}
inline folly::IOBuf* bufPtr(folly::IOBuf& buf) {
  return &buf;
}
} // detail

template <class R>
typename std::enable_if<R::hasValue, const folly::IOBuf*>::type
valuePtrUnsafe(const R& requestOrReply) {
  return detail::bufPtr(const_cast<R&>(requestOrReply).value());
}
template <class R>
typename std::enable_if<R::hasValue, folly::IOBuf*>::type
valuePtrUnsafe(R& requestOrReply) {
  return detail::bufPtr(requestOrReply.value());
}
template <class R>
typename std::enable_if<!R::hasValue, folly::IOBuf*>::type
valuePtrUnsafe(const R& requestOrReply) {
  return nullptr;
}

template <class R>
typename std::enable_if<R::hasValue, folly::StringPiece>::type
valueRangeSlow(R& requestOrReply) {
  auto* buf = detail::bufPtr(requestOrReply.value());
  return buf ? folly::StringPiece(buf->coalesce()) : folly::StringPiece();
}
template <class R>
typename std::enable_if<!R::hasValue, folly::StringPiece>::type
valueRangeSlow(R& requestOrReply) {
  return folly::StringPiece();
}

// Helper class to determine whether a type is a Carbon request.
template <class Msg>
class IsRequestTrait {
  template <class T>
  static std::true_type check(typename T::reply_type*);
  template <class T>
  static std::false_type check(...);

 public:
  static constexpr bool value = decltype(check<Msg>(0))::value;
};

namespace detail {
// Utility class useful for checking whether a particular OnRequest handler
// class defines an onRequest() handler for Request.
class CanHandleRequest {
  template <class R, class O>
  static constexpr auto check(int) -> decltype(
      std::declval<O>().onRequest(
          std::declval<facebook::memcache::McServerRequestContext>(),
          std::declval<R>()),
      std::true_type()) {
    return {};
  }

  template <class R, class O>
  static constexpr std::false_type check(...) {
    return {};
  }
 public:
  template <class Request, class OnRequest>
  static constexpr auto value() -> decltype(check<Request, OnRequest>(0)) {
    return {};
  }
};
} // detail

} // carbon
