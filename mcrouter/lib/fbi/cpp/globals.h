/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdint.h>

namespace facebook {
namespace memcache {
namespace globals {

/**
 * @return lazy-initialized hostid.
 */
uint32_t hostid();

/**
 * FOR TEST PURPOSES ONLY
 *
 * Allows to override hostid for testing purposes, resets it on destruction.
 */
struct HostidMock {
  explicit HostidMock(uint32_t value);
  void reset();
  ~HostidMock() {
    reset();
  }
};
}
}
} // facebook::memcache::globals
