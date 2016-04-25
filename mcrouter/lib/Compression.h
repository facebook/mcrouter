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

#include <memory>

namespace folly {
class IOBuf;
}

namespace facebook {
namespace memcache {

enum class CompressionCodecType {
  // Does not compress.
  // Thread-safe.
  // Doesn't need uncompressed size.
  NO_COMPRESSION,

  // Use LZ4 compression.
  // Not thread-safe.
  // Requires uncompressed size.
  LZ4,
};

/**
 * Dictionary-based compression codec.
 */
class CompressionCodec {
 public:
  virtual ~CompressionCodec() {}

  /**
   * Compress data.
   *
   * @param data  Data to compress.
   * @return      Compressed data.
   *
   * @throw std::runtime_error    On compression error.
   * @throw std::bad_alloc        On error to allocate output buffer.
   */
  virtual std::unique_ptr<folly::IOBuf> compress(const folly::IOBuf& data) = 0;

  /**
   * Uncompress data.
   *
   * @param data                Compressed data to uncompress.
   * @param uncompressedLength  Size of the uncompressed data.
   * @return                    Uncompressed data.
   *
   * @throw std::invalid_argument If the codec expects uncompressedLength,
   *                              but 0 is provided.
   * @throw std::runtime_error    On uncompresion error.
   * @throw std::bad_alloc        On error to allocate output buffer.
   */
  virtual std::unique_ptr<folly::IOBuf> uncompress(
      const folly::IOBuf& data, size_t uncompressedLength = 0) = 0;

  /**
   * Return the codec's type.
   */
  CompressionCodecType type() const { return type_; }

 protected:
  /**
   * Builds the compression codec
   *
   * @param type        Compression algorithm to use.
   * @param dictionary  Pre-defined dictionary to compress/uncompress data.
   */
  explicit CompressionCodec(CompressionCodecType type);

 private:
  const CompressionCodecType type_;
};

/**
 * Creates a compression codec with a given pre-defined dictionary.
 *
 * @throw std::runtime_error    On any error to create the codec.
 */
std::unique_ptr<CompressionCodec> createCompressionCodec(
    CompressionCodecType type, std::unique_ptr<folly::IOBuf> dictionary);

} // memcache
} // facebook
