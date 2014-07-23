/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <cassert>

namespace facebook { namespace memcache {

template <typename F>
void Fiber::setFunction(F&& func) {
  assert(state_ == INVALID);
  func_ = std::move(func);
  state_ = NOT_STARTED;
}

}}  // facebook::memcache
