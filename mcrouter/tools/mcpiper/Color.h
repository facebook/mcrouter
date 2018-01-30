/*
 *  Copyright (c) 2013-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cstdint>

namespace facebook {
namespace memcache {

enum class Color : uint8_t {
  DEFAULT,
  BLACK,
  BLUE,
  DARKBLUE,
  CYAN,
  DARKCYAN,
  GRAY,
  DARKGRAY,
  GREEN,
  DARKGREEN,
  MAGENTA,
  DARKMAGENTA,
  RED,
  DARKRED,
  WHITE,
  YELLOW,
  DARKYELLOW
};
}
} // facebook::memcache
