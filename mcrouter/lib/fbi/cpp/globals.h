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

#include <stdint.h>

namespace facebook { namespace memcache { namespace globals {

/**
 * @return lazy-initialized hostid.
 */
uint32_t hostid();

}}} // facebook::memcache::globals
