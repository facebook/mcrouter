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

namespace carbon {
namespace util {

inline uint16_t zigzag(const int16_t i) {
  const uint16_t ui = static_cast<uint16_t>(i);
  return (ui << 1) ^ (ui >> 15);
}

inline uint32_t zigzag(const int32_t i) {
  const uint32_t ui = static_cast<uint32_t>(i);
  return (ui << 1) ^ (ui >> 31);
}

inline uint64_t zigzag(const int64_t i) {
  const uint64_t ui = static_cast<uint64_t>(i);
  return (ui << 1) ^ (ui >> 63);
}

inline int16_t unzigzag(const uint16_t i) {
  return (i >> 1) ^ (i << 15);
}

inline int32_t unzigzag(const uint32_t i) {
  return (i >> 1) ^ (i << 31);
}

inline int64_t unzigzag(const uint64_t i) {
  return (i >> 1) ^ (i << 63);
}

} // util
} // carbon
