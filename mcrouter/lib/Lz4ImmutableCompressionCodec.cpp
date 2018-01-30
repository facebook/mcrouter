/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Lz4ImmutableCompressionCodec.h"

namespace facebook {
namespace memcache {

Lz4ImmutableCompressionCodec::Lz4ImmutableCompressionCodec(
    std::unique_ptr<folly::IOBuf> dictionary,
    uint32_t id,
    FilteringOptions codecFilteringOptions,
    uint32_t codecCompressionLevel)
    : CompressionCodec(
          CompressionCodecType::LZ4Immutable,
          id,
          codecFilteringOptions,
          codecCompressionLevel),
      codec_(std::move(dictionary)) {}

std::unique_ptr<folly::IOBuf> Lz4ImmutableCompressionCodec::compress(
    const struct iovec* iov,
    size_t iovcnt) {
  return codec_.compress(iov, iovcnt);
}

std::unique_ptr<folly::IOBuf> Lz4ImmutableCompressionCodec::uncompress(
    const struct iovec* iov,
    size_t iovcnt,
    size_t uncompressedSize) {
  return codec_.decompress(iov, iovcnt, uncompressedSize);
}

} // memcache
} // facebook
