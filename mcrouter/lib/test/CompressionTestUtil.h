/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>
#include <unordered_map>

#include "mcrouter/lib/CompressionCodecManager.h"

namespace facebook {
namespace memcache {
namespace test {

// Utility function used to create a random string of length size.
std::string createBinaryData(size_t size);

// Static CodecConfig map that can be used in tests to initialize a
// CompressionCodecManager.
std::unordered_map<uint32_t, CodecConfigPtr> testCodecConfigs();

} // test
} // memcache
} // facebook
