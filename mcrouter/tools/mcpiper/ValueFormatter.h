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

#include <folly/String.h>

#include "mcrouter/tools/mcpiper/PrettyFormat.h"
#include "mcrouter/tools/mcpiper/StyledString.h"

namespace facebook { namespace memcache {

/**
 * Formats the value part of McRequest/McReply.
 */
class ValueFormatter {
 public:
  ValueFormatter() = default;
  virtual ~ValueFormatter() = default;

  // Non-copyable
  ValueFormatter(const ValueFormatter&) = delete;
  ValueFormatter& operator=(const ValueFormatter&) = delete;

  // Non-movable
  ValueFormatter(ValueFormatter&&) = delete;
  ValueFormatter&& operator=(ValueFormatter&&) = delete;

  /**
   * Given a raw MC value and its flags, perform best effort uncompression
   * and formatting.
   *
   * @param value             The value to uncompress and format.
   * @param flags             Flags
   * @param format            Format to use.
   * @param uncompressedSize  Output parameter containing the uncompressed size.
   * @return                  The formatted value.
   */
  virtual StyledString uncompressAndFormat(folly::StringPiece value,
                                           uint64_t flags,
                                           PrettyFormat format,
                                           size_t& uncompressedSize) noexcept {
    uncompressedSize = value.size();
    return StyledString(value.str(), format.dataValueColor);
  }
};

}} // facebook::memcache
