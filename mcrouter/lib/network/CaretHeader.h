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

#include <utility>

#include <folly/Varint.h>

#include "mcrouter/lib/network/ServerLoad.h"

namespace facebook {
namespace memcache {

constexpr char kCaretMagicByte = '^';
constexpr size_t kMaxAdditionalFields = 6;
constexpr size_t kMaxHeaderLength = 1 /* magic byte */ +
    1 /* GroupVarint header (lengths of 4 ints) */ +
    4 * sizeof(uint32_t) /* body size, typeId, reqId, num additional fields */ +
    2 * kMaxAdditionalFields * folly::kMaxVarintLength64; /* key and value for
                                                          additional fields */

// Normalize the dropProbability to the accuracy of 10^-6.
constexpr uint32_t kDropProbabilityNormalizer = 1000000;

constexpr uint32_t kCaretConnectionControlReqId = 0;

enum class UmbrellaVersion : uint8_t {
  BASIC = 0,
  TYPED_MESSAGE = 1,
};

struct UmbrellaMessageInfo {
  uint32_t headerSize;
  uint32_t bodySize;
  UmbrellaVersion version;
  uint32_t typeId;
  uint32_t reqId;

  // Additional fields
  std::pair<uint64_t, uint64_t> traceId{0, 0};
  uint64_t supportedCodecsFirstId{0};
  uint64_t supportedCodecsSize{0};
  uint64_t usedCodecId{0};
  uint64_t uncompressedBodySize{0};
  uint64_t dropProbability{0}; // Use uint64_t to store a double.
  ServerLoad serverLoad{0};
};

enum class CaretAdditionalFieldType {
  TRACE_ID = 0,

  // Range of supportted codecs
  SUPPORTED_CODECS_FIRST_ID = 1,
  SUPPORTED_CODECS_SIZE = 2,

  // Id of codec used to compress the data.
  USED_CODEC_ID = 3,

  // Size of body after decompression.
  UNCOMPRESSED_BODY_SIZE = 4,

  // Drop Probability of each request.
  DROP_PROBABILITY = 5,

  // Node ID for trace
  TRACE_NODE_ID = 6,

  // Load on the server
  SERVER_LOAD = 7,
};

} // memcache
} // facebook
