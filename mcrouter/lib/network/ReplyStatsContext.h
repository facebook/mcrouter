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

namespace facebook {
namespace memcache {

/*
 * Used to pass useful stats and information from AsyncMcClient back up to the
 * routing layer (DestinationRoute).
 *
 * Contains id of codec used for compressing reply, reply size before and after
 * compression. If no compression is used, then usedCodecId is zero.
 */
struct ReplyStatsContext {
  ReplyStatsContext() = default;
  ReplyStatsContext(
      uint32_t usedCodecId_,
      uint32_t replySizeBeforeCompression_,
      uint32_t replySizeAfterCompression_)
      : usedCodecId(usedCodecId_),
        replySizeBeforeCompression(replySizeBeforeCompression_),
        replySizeAfterCompression(replySizeAfterCompression_) {}

  uint32_t usedCodecId{0};
  uint32_t replySizeBeforeCompression{0};
  uint32_t replySizeAfterCompression{0};
};

} // memcache
} // facebook
