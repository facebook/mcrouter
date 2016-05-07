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

#include "mcrouter/lib/IovecCursor.h"
#include "mcrouter/lib/Lz4Immutable.h"

namespace facebook {
namespace memcache {

CompressionCodec::CompressionCodec(
    CompressionCodecType type,
    uint32_t id,
    CompressionCodecOptions options)
    : type_(type), id_(id), options_(options) {}

std::unique_ptr<folly::IOBuf> CompressionCodec::compress(
    const folly::IOBuf& data) {
  auto iov = data.getIov();
  return compress(iov.data(), iov.size());
}
std::unique_ptr<folly::IOBuf> CompressionCodec::compress(
    const void* data,
    size_t len) {
  struct iovec iov;
  iov.iov_base = const_cast<void*>(data);
  iov.iov_len = len;
  return compress(&iov, 1);
}

std::unique_ptr<folly::IOBuf> CompressionCodec::uncompress(
    const folly::IOBuf& data,
    size_t uncompressedLength) {
  auto iov = data.getIov();
  return uncompress(iov.data(), iov.size(), uncompressedLength);
}
std::unique_ptr<folly::IOBuf> CompressionCodec::uncompress(
    const void* data,
    size_t len,
    size_t uncompressedLength) {
  struct iovec iov;
  iov.iov_base = const_cast<void*>(data);
  iov.iov_len = len;
  return uncompress(&iov, 1, uncompressedLength);
}

namespace {

std::unique_ptr<folly::IOBuf> wrapIovec(
    const struct iovec* iov,
    size_t iovcnt) {
  if (iovcnt == 0) {
    return nullptr;
  }

  auto head = folly::IOBuf::wrapBuffer(iov[0].iov_base, iov[0].iov_len);
  for (size_t i = iovcnt - 1; i > 0; --i) {
    head->appendChain(
        folly::IOBuf::wrapBuffer(iov[i].iov_base, iov[i].iov_len));
  }
  return head;
}

folly::IOBuf
coalesceSlow(const struct iovec* iov, size_t iovcnt, size_t destCapacity) {
  folly::IOBuf buffer(folly::IOBuf::CREATE, destCapacity);
  for (size_t i = 0; i < iovcnt; ++i) {
    std::memcpy(buffer.writableTail(), iov[i].iov_base, iov[i].iov_len);
    buffer.append(iov[i].iov_len);
  }
  assert(buffer.length() <= destCapacity);
  return buffer;
}

folly::IOBuf
coalesce(const struct iovec* iov, size_t iovcnt, size_t destCapacity) {
  if (iovcnt == 1) {
    return folly::IOBuf(
        folly::IOBuf::WRAP_BUFFER, iov[0].iov_base, iov[0].iov_len);
  }
  return coalesceSlow(iov, iovcnt, destCapacity);
}

/************************
 * No Compression Codec *
 ************************/
class NoCompressionCodec : public CompressionCodec {
 public:
  NoCompressionCodec(
      std::unique_ptr<folly::IOBuf> dictionary,
      uint32_t id,
      CompressionCodecOptions options)
      : CompressionCodec(CompressionCodecType::NO_COMPRESSION, id, options) {}

  std::unique_ptr<folly::IOBuf> compress(const struct iovec* iov, size_t iovcnt)
      override final {
    return wrapIovec(iov, iovcnt);
  }
  std::unique_ptr<folly::IOBuf> uncompress(
      const struct iovec* iov,
      size_t iovcnt,
      size_t uncompressedLength = 0) override final {
    return wrapIovec(iov, iovcnt);
  }
};

/*************************
 * LZ4 Compression Codec *
 *************************/
#if FOLLY_HAVE_LIBLZ4
class Lz4CompressionCodec : public CompressionCodec {
 public:
  Lz4CompressionCodec(
      std::unique_ptr<folly::IOBuf> dictionary,
      uint32_t id,
      CompressionCodecOptions options);

  std::unique_ptr<folly::IOBuf> compress(const struct iovec* iov, size_t iovcnt)
      override final;
  std::unique_ptr<folly::IOBuf> uncompress(
      const struct iovec* iov,
      size_t iovcnt,
      size_t uncompressedLength = 0) override final;

 private:
  static constexpr size_t kMaxDictionarySize = 64 * 1024;
  static constexpr size_t kLargeDataThreshold = 2 * 1024;

  const std::unique_ptr<folly::IOBuf> dictionary_;
  const Lz4Immutable lz4Immutable_;
  LZ4_stream_t* lz4Stream_{nullptr};

  FOLLY_NOINLINE std::unique_ptr<folly::IOBuf> compressLargeData(
      folly::IOBuf data);
};

Lz4CompressionCodec::Lz4CompressionCodec(
    std::unique_ptr<folly::IOBuf> dictionary,
    uint32_t id,
    CompressionCodecOptions options)
    : CompressionCodec(CompressionCodecType::LZ4, id, options),
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
    const struct iovec* iov,
    size_t iovcnt) {
  assert(iov);

  auto size = IovecCursor::computeTotalLength(iov, iovcnt);
  if (size < kLargeDataThreshold) {
    return lz4Immutable_.compress(iov, iovcnt);
  }
  return compressLargeData(coalesce(iov, iovcnt, size));
}

std::unique_ptr<folly::IOBuf> Lz4CompressionCodec::compressLargeData(
    folly::IOBuf data) {
  LZ4_stream_t lz4StreamCopy = *lz4Stream_;

  size_t compressBound = LZ4_compressBound(data.length());
  auto buffer = folly::IOBuf::create(compressBound);

  int compressedSize = LZ4_compress_fast_continue(
      &lz4StreamCopy,
      reinterpret_cast<const char*>(data.data()),
      reinterpret_cast<char*>(buffer->writableTail()),
      data.length(),
      compressBound,
      1);

  // compression is guaranteed to work as we use
  // LZ4_compressBound as destBuffer size.
  assert(compressedSize > 0);

  buffer->append(compressedSize);
  return buffer;
}

std::unique_ptr<folly::IOBuf> Lz4CompressionCodec::uncompress(
    const struct iovec* iov,
    size_t iovcnt,
    size_t uncompressedLength) {
  if (uncompressedLength == 0) {
    throw std::invalid_argument("LZ4 codec: uncompressed length required");
  }

  auto data =
      coalesce(iov, iovcnt, IovecCursor::computeTotalLength(iov, iovcnt));
  auto buffer = folly::IOBuf::create(uncompressedLength);
  int bytesWritten = LZ4_decompress_safe_usingDict(
      reinterpret_cast<const char*>(data.data()),
      reinterpret_cast<char*>(buffer->writableTail()),
      data.length(), buffer->tailroom(),
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
    CompressionCodecType type,
    std::unique_ptr<folly::IOBuf> dictionary,
    uint32_t id,
    CompressionCodecOptions options) {
  switch (type) {
    case CompressionCodecType::NO_COMPRESSION:
      return folly::make_unique<NoCompressionCodec>(
          std::move(dictionary), id, options);
    case CompressionCodecType::LZ4:
#if FOLLY_HAVE_LIBLZ4
      return folly::make_unique<Lz4CompressionCodec>(
          std::move(dictionary), id, options);
#else
      LOG(ERROR) << "LZ4 is not available. Returning nullptr.";
      return nullptr;
#endif // FOLLY_HAVE_LIBLZ4
  }
  return nullptr;
}

} // memcache
} // facebook
