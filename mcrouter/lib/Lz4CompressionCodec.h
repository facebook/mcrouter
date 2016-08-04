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

#include <folly/Format.h>

#if FOLLY_HAVE_LIBLZ4
#include "mcrouter/lib/Compression.h"
#include "mcrouter/lib/IovecCursor.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/Lz4Immutable.h"

#include <lz4.h>

namespace facebook {
namespace memcache {

class Lz4CompressionCodec : public CompressionCodec {
 public:
  Lz4CompressionCodec(
      std::unique_ptr<folly::IOBuf> dictionary,
      uint32_t id,
      CompressionCodecOptions options,
      bool isCodecEnabled);

  std::unique_ptr<folly::IOBuf> compress(const struct iovec* iov, size_t iovcnt)
      override final;
  std::unique_ptr<folly::IOBuf> uncompress(
      const struct iovec* iov,
      size_t iovcnt,
      size_t uncompressedLength = 0) override final;

  ~Lz4CompressionCodec();

 private:
  static constexpr size_t kMaxDictionarySize = 64 * 1024;

  const std::unique_ptr<folly::IOBuf> dictionary_;
  const Lz4Immutable lz4Immutable_;
  LZ4_stream_t* lz4Stream_{nullptr};

  FOLLY_NOINLINE std::unique_ptr<folly::IOBuf> compressLargeData(
      folly::IOBuf data);
};

} // memcache
} // facebook
#endif // FOLLY_HAVE_LIBLZ4
