/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>

#include "mcrouter/tools/mcpiper/ValueFormatter.h"

namespace facebook { namespace memcache {

/**
 * Returns the default fifo root.
 */
std::string getDefaultFifoRoot();

/**
 * Creates value formatter.
 */
std::unique_ptr<ValueFormatter> createValueFormatter();

}} // facebook::memcache
