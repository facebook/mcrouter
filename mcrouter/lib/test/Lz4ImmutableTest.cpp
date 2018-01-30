/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/Format.h>
#include <folly/Random.h>
#include <folly/io/IOBuf.h>
#include <lz4.h>

#include "mcrouter/lib/Lz4Immutable.h"

using namespace facebook::memcache;

namespace {

std::unique_ptr<folly::IOBuf> getAsciiDictionary() {
  static const char dic[] =
      "VALUE key 0 10\r\n"
      "0123456789\r\n"
      "END\r\n"
      "CLIENT_ERROR malformed request\r\n"
      "VALUE anotherkey 0 12\r\n"
      "anothervalue\r\n"
      "END\r\n"
      "END\r\n"
      "VALUE test.aap.j 0 50\r\n"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n"
      "END\r\n"
      "END\r\n";
  return folly::IOBuf::wrapBuffer(
      reinterpret_cast<const uint8_t*>(dic), sizeof(dic));
}

std::unique_ptr<folly::IOBuf> getAsciiData() {
  static const char reply[] =
      "VALUE test.aap.f 0 12\r\n"
      "thisisavalue\r\n"
      "END\r\n";
  return folly::IOBuf::wrapBuffer(
      reinterpret_cast<const uint8_t*>(reply), sizeof(reply));
}

std::unique_ptr<folly::IOBuf> getRandomAsciiData(
    size_t minSize = 1024,
    size_t maxSize = 64 * 1024) {
  CHECK_GT(maxSize, minSize);

  static const char alphabet[] =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  uint32_t size = folly::Random::rand32(minSize, maxSize);
  std::string reply;
  reply.reserve(size);
  reply.append(folly::sformat("VALUE test.aap.f 0 {}\r\n", size));
  for (size_t i = 0; i < size; ++i) {
    reply.push_back(alphabet[folly::Random::rand32(0, sizeof(alphabet))]);
  }
  reply.append("\r\nEND\r\n");

  return folly::IOBuf::copyBuffer(std::move(reply));
}

std::unique_ptr<folly::IOBuf> getRandomBinaryData(size_t size) {
  auto buffer = folly::IOBuf::create(size);
  auto data = buffer->writableData();
  for (size_t i = 0; i < size; ++i) {
    uint32_t val = folly::Random::rand32(0, sizeof(uint8_t));
    *data = static_cast<uint8_t>(val);
    ++data;
  }
  buffer->append(size);
  return buffer;
}

std::unique_ptr<folly::IOBuf> getRandomBinaryCompressibleData(
    const folly::IOBuf& dictionary,
    size_t minSize = 1024,
    size_t maxSize = 64 * 1024) {
  CHECK_GT(maxSize, minSize);

  uint32_t size = folly::Random::rand32(minSize, maxSize);
  auto buffer = folly::IOBuf::create(size);
  auto data = buffer->writableData();
  const auto end = data + size;
  while (data < end) {
    size_t chunckSize = folly::Random::rand32(
        1,
        std::min(dictionary.length(), static_cast<uint64_t>(end - data + 1)));

    std::unique_ptr<folly::IOBuf> source;
    size_t sourceOffset;
    if (folly::Random::oneIn(2)) {
      // grab data from dictionary sometimes to be compressible.
      source = dictionary.clone();
      sourceOffset = folly::Random::rand32(source->length() - chunckSize + 1);
    } else {
      source = getRandomBinaryData(chunckSize);
      sourceOffset = 0;
    }
    std::memcpy(data, source->data() + sourceOffset, chunckSize);
    data += chunckSize;
  }

  buffer->append(size);
  return buffer;
}

std::unique_ptr<folly::IOBuf> buildChain(
    const folly::IOBuf& data,
    size_t chainLength) {
  CHECK_GT(chainLength, 1);
  CHECK(!data.isChained());
  CHECK_GE(data.length(), chainLength);

  size_t partSize = data.length() / chainLength;
  size_t lastPartSize = data.length() - (partSize * (chainLength - 1));
  size_t bufSize = std::max(partSize, lastPartSize);
  auto head = folly::IOBuf::create(bufSize);

  auto cur = head.get();
  size_t i;
  for (i = 0; i < (chainLength - 1); ++i) {
    std::memcpy(cur->writableTail(), data.data() + (partSize * i), partSize);
    cur->append(partSize);
    cur->appendChain(folly::IOBuf::create(bufSize));
    cur = cur->next();
  }
  std::memcpy(cur->writableTail(), data.data() + (partSize * i), lastPartSize);
  cur->append(lastPartSize);

  assert(head->isChained());
  assert(chainLength == head->countChainElements());
  assert(data.length() == head->computeChainDataLength());

  return head;
}

std::unique_ptr<folly::IOBuf> lz4Decompress(
    std::unique_ptr<folly::IOBuf> dictionary,
    folly::IOBuf& data,
    size_t uncompressedLength) {
  auto bytes = data.coalesce();
  auto buffer = folly::IOBuf::create(uncompressedLength);
  int ret = LZ4_decompress_safe_usingDict(
      reinterpret_cast<const char*>(bytes.data()),
      reinterpret_cast<char*>(buffer->writableTail()),
      data.length(),
      buffer->tailroom(),
      reinterpret_cast<const char*>(dictionary->data()),
      dictionary->length());

  assert(ret >= 0);

  auto const bytesWritten = static_cast<size_t>(ret);
  // Should either fail completely or decompress everything.
  assert(bytesWritten == uncompressedLength);

  buffer->append(bytesWritten);
  return buffer;
}

} // anonymous namespace

TEST(Lz4Immutable, lz4Compatibility_ascii) {
  auto dictionary = getAsciiDictionary();
  Lz4Immutable compressor(dictionary->clone());

  auto source = getAsciiData();
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed =
      lz4Decompress(dictionary->clone(), *compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}
TEST(Lz4Immutable, lz4Compatibility_binary) {
  auto dictionary = getRandomBinaryData(64 * 1024);
  Lz4Immutable compressor(dictionary->clone());

  auto source = getRandomBinaryCompressibleData(*dictionary);
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed =
      lz4Decompress(dictionary->clone(), *compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}

TEST(Lz4Immutable, emptyData) {
  auto dictionary = getAsciiDictionary();
  Lz4Immutable compressor(dictionary->clone());

  auto source = folly::IOBuf::create(0);
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}

TEST(Lz4Immutable, largeData_ascii) {
  auto dictionary = getAsciiDictionary();
  Lz4Immutable compressor(dictionary->clone());

  auto source = getRandomAsciiData();
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}
TEST(Lz4Immutable, largeData_binary) {
  auto dictionary = getRandomBinaryData(64 * 1024);
  Lz4Immutable compressor(dictionary->clone());

  auto source = getRandomBinaryCompressibleData(*dictionary);
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}

TEST(Lz4Immutable, hugeData_ascii) {
  auto dictionary = getAsciiDictionary();
  Lz4Immutable compressor(dictionary->clone());

  auto source = getRandomAsciiData(64 * 1024, 256 * 1024);
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}
TEST(Lz4Immutable, hugeData_binary) {
  auto dictionary = getRandomBinaryData(64 * 1024);
  Lz4Immutable compressor(dictionary->clone());

  auto source =
      getRandomBinaryCompressibleData(*dictionary, 64 * 1024, 256 * 1024);
  auto sourceSize = source->computeChainDataLength();

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}

TEST(Lz4Immutable, chained_ascii) {
  auto dictionary = getAsciiDictionary();
  Lz4Immutable compressor(dictionary->clone());

  auto tmpSource = getRandomAsciiData();
  auto source =
      buildChain(*tmpSource, folly::Random::rand32(2, tmpSource->length()));
  auto sourceSize = source->computeChainDataLength();
  EXPECT_EQ(tmpSource->length(), source->computeChainDataLength());

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}
TEST(Lz4Immutable, chained_binary) {
  auto dictionary = getRandomBinaryData(64 * 1024);
  Lz4Immutable compressor(dictionary->clone());

  auto tmpSource = getRandomBinaryCompressibleData(*dictionary);
  auto source =
      buildChain(*tmpSource, folly::Random::rand32(2, tmpSource->length()));
  auto sourceSize = source->computeChainDataLength();
  EXPECT_EQ(tmpSource->length(), source->computeChainDataLength());

  // Compress
  auto compressed = compressor.compress(*source);

  // Uncompress
  auto decompressed = compressor.decompress(*compressed, sourceSize);

  EXPECT_EQ(sourceSize, decompressed->computeChainDataLength());
  auto sourceStr = source->moveToFbString();
  auto decompressedStr = decompressed->moveToFbString();
  EXPECT_EQ(sourceStr, decompressedStr);
}
