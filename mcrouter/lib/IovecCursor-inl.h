/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cstring>
#include <type_traits>

#include <folly/lang/Bits.h>

namespace facebook {
namespace memcache {

template <class T>
T IovecCursor::peek() const {
  static_assert(std::is_integral<T>::value, "Read requires an integral type");

  if (LIKELY(curBufLen_ >= sizeof(T))) {
    return folly::loadUnaligned<T>(
        reinterpret_cast<uint8_t*>(iov_[iovIndex_].iov_base) + curBufPos_);
  }

  uint8_t buf[sizeof(T)];
  peekInto(buf, sizeof(T));
  T val;
  std::memcpy(&val, buf, sizeof(T));
  return val;
}

template <class T>
T IovecCursor::read() {
  T val = peek<T>();
  advance(sizeof(T));
  return val;
}

} // memcache
} // facebook
