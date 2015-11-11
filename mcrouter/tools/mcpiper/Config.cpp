/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Config.h"

#include <folly/Memory.h>

namespace facebook { namespace memcache {

std::string getDefaultFifoRoot() {
  return "/var/mcrouter/fifos";
}

std::unique_ptr<ValueFormatter> createValueFormatter() {
  return folly::make_unique<ValueFormatter>();
}

}} // facebook::memcache
