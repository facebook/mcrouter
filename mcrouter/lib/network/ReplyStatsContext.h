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

#include <stddef.h>

namespace facebook {
namespace memcache {

/*
 * Contains id of codec used for compressing reply,
 * reply size before and after compression.
 *
 * NOTICE: if no compression is used then usedCodecId equals to zero.
 */
struct ReplyStatsContext {
  uint32_t usedCodecId{0};
  uint32_t replySizeBeforeCompression{0};
  uint32_t replySizeAfterCompression{0};
  ReplyStatsContext() = default;
  ReplyStatsContext(
      uint32_t usedCodecId_,
      uint32_t replySizeBeforeCompression_,
      uint32_t replySizeAfterCompression_)
      : usedCodecId(usedCodecId_),
        replySizeBeforeCompression(replySizeBeforeCompression_),
        replySizeAfterCompression(replySizeAfterCompression_) {}
};

} // memcache
} // facebook
