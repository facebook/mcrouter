/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Compression.h"

#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/io/IOBuf.h>

#if FOLLY_HAVE_LIBLZ4
#include <lz4.h>
#endif // FOLLY_HAVE_LIBLZ4

#include "mcrouter/lib/Lz4Immutable.h"

namespace facebook {
namespace memcache {

CompressionCodec::CompressionCodec(CompressionCodecType type) : type_(type) {}

namespace {

/************************
 * No Compression Codec *
 ************************/
class NoCompressionCodec : public CompressionCodec {
 public:
  explicit NoCompressionCodec(std::unique_ptr<folly::IOBuf>)
      : CompressionCodec(CompressionCodecType::NO_COMPRESSION) {}

  std::unique_ptr<folly::IOBuf>
  compress(const folly::IOBuf& data) override final {
    return data.clone();
  }
  std::unique_ptr<folly::IOBuf> uncompress(
      const folly::IOBuf& data, size_t uncompressedLength = 0) override final {
    return data.clone();
  }
};

/*************************
 * LZ4 Compression Codec *
 *************************/
#if FOLLY_HAVE_LIBLZ4
class Lz4CompressionCodec : public CompressionCodec {
 public:
  explicit Lz4CompressionCodec(std::unique_ptr<folly::IOBuf> dictionary);

  std::unique_ptr<folly::IOBuf>
  compress(const folly::IOBuf& data) override final;
  std::unique_ptr<folly::IOBuf> uncompress(
      const folly::IOBuf& data, size_t uncompressedLength = 0) override final;

 private:
  static constexpr size_t kMaxDictionarySize = 64 * 1024;
  static constexpr size_t kLargeDataThreshold = 2 * 1024;

  const std::unique_ptr<folly::IOBuf> dictionary_;
  const Lz4Immutable lz4Immutable_;
  LZ4_stream_t* lz4Stream_{nullptr};

  FOLLY_NOINLINE std::unique_ptr<folly::IOBuf> compressLargeData(
      const folly::IOBuf& data);
};

Lz4CompressionCodec::Lz4CompressionCodec(
    std::unique_ptr<folly::IOBuf> dictionary)
    : CompressionCodec(CompressionCodecType::LZ4),
      dictionary_(std::move(dictionary)),
      lz4Immutable_(dictionary_->clone()),
      lz4Stream_(LZ4_createStream()) {
  if (!lz4Stream_) {
    throw std::runtime_error("Failed to allocate LZ4_stream_t");
  }

  int res = LZ4_loadDict(
      lz4Stream_,
      reinterpret_cast<const char*>(dictionary_->data()),
      dictionary_->length());
  if (res != dictionary_->length()) {
    throw std::runtime_error(
        folly::sformat(
            "LZ4 codec: Failed to load dictionary. Return code: {}", res));
  }
}

std::unique_ptr<folly::IOBuf> Lz4CompressionCodec::compress(
    const folly::IOBuf& data) {
  auto size = data.computeChainDataLength();
  if (size < kLargeDataThreshold) {
    return lz4Immutable_.compress(data);
  }
  return compressLargeData(data);
}

std::unique_ptr<folly::IOBuf> Lz4CompressionCodec::compressLargeData(
    const folly::IOBuf& data) {
  LZ4_stream_t lz4StreamCopy = *lz4Stream_;
  auto dataClone = data.clone();

  auto bytes = dataClone->coalesce();
  size_t compressBound = LZ4_compressBound(bytes.size());
  auto buffer = folly::IOBuf::create(compressBound);

  int compressedSize = LZ4_compress_fast_continue(
      &lz4StreamCopy,
      reinterpret_cast<const char*>(bytes.data()),
      reinterpret_cast<char*>(buffer->writableTail()),
      bytes.size(),
      compressBound,
      1);

  // compression is guaranteed to work as we use
  // LZ4_compressBound as destBuffer size.
  assert(compressedSize > 0);

  buffer->append(compressedSize);
  return buffer;
}

std::unique_ptr<folly::IOBuf> Lz4CompressionCodec::uncompress(
    const folly::IOBuf& data, size_t uncompressedLength) {
  if (uncompressedLength == 0) {
    throw std::invalid_argument("LZ4 codec: uncompressed length required");
  }

  auto dataClone = data.clone();
  auto bytes = dataClone->coalesce();
  auto buffer = folly::IOBuf::create(uncompressedLength);
  int bytesWritten = LZ4_decompress_safe_usingDict(
      reinterpret_cast<const char*>(bytes.data()),
      reinterpret_cast<char*>(buffer->writableTail()),
      bytes.size(), buffer->tailroom(),
      reinterpret_cast<const char*>(dictionary_->data()),
      dictionary_->length());

  // Should either fail completely or decompress everything.
  assert(bytesWritten <= 0 || bytesWritten == uncompressedLength);
  if (bytesWritten <= 0) {
    throw std::runtime_error("LZ4 codec: decompression returned invalid value");
  }

  buffer->append(bytesWritten);
  return buffer;
}
#endif // FOLLY_HAVE_LIBLZ4

} // anonymous namespace

/*****************************
 * Compression Codec Factory *
 *****************************/
std::unique_ptr<CompressionCodec> createCompressionCodec(
    CompressionCodecType type, std::unique_ptr<folly::IOBuf> dictionary) {
  switch (type) {
    case CompressionCodecType::NO_COMPRESSION:
      return folly::make_unique<NoCompressionCodec>(std::move(dictionary));
    case CompressionCodecType::LZ4:
#if FOLLY_HAVE_LIBLZ4
      return folly::make_unique<Lz4CompressionCodec>(std::move(dictionary));
#else
      LOG(ERROR) << "LZ4 is not available. Returning nullptr.";
      return nullptr;
#endif // FOLLY_HAVE_LIBLZ4
  }
  return nullptr;
}
} // memcache
} // facebook
