/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <limits>
#include <memory>

#include <gtest/gtest.h>

#include <folly/Memory.h>
#include <folly/Random.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/CompressionCodecManager.h"

namespace facebook { namespace memcache { namespace test {

namespace {

std::string createBinaryData(size_t size) {
  std::string dic;
  dic.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    dic.push_back(static_cast<char>(
          folly::Random::rand32(0, std::numeric_limits<char>::max() + 1)));
  }
  return dic;
}

void validateCodec(CompressionCodec* codec) {
  EXPECT_TRUE(codec);

  auto data = createBinaryData(folly::Random::rand32(1, 16 * 1024));
  auto buf = folly::IOBuf::wrapBuffer(data.data(), data.size());
  auto compressedData = codec->compress(*buf);
  EXPECT_TRUE(compressedData);

  auto uncompressedData = codec->uncompress(*compressedData, data.size());
  EXPECT_EQ(data.size(), uncompressedData->computeChainDataLength());
  EXPECT_EQ(buf->coalesce(), uncompressedData->coalesce());
}

}  // anonymous namespace

TEST(CompressionCodecManager, basic) {
  std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs;
  for (uint32_t i = 1; i <= 64; ++ i) {
    codecConfigs.emplace(
        i,
        folly::make_unique<CodecConfig>(
            i, CompressionCodecType::LZ4, createBinaryData(i * 1024)));
  }

  CompressionCodecManager codecManager(std::move(codecConfigs));
  auto codecMap = codecManager.getCodecMap();

  for (uint32_t i = 1; i <= 64; ++ i) {
    validateCodec(codecMap->get(i));
  }
}

TEST(CompressionCodecManager, missingStart) {
  std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs;
  for (uint32_t i = 10; i <= 64; ++ i) {
    codecConfigs.emplace(
        i,
        folly::make_unique<CodecConfig>(
            i, CompressionCodecType::LZ4, createBinaryData(i * 1024)));
  }

  CompressionCodecManager codecManager(std::move(codecConfigs));
  auto codecMap = codecManager.getCodecMap();

  for (uint32_t i = 1; i <= 64; ++ i) {
    if (i < 10) {
      EXPECT_FALSE(codecMap->get(i));
    } else {
      validateCodec(codecMap->get(i));
    }
  }
}

TEST(CompressionCodecManager, missingMiddle) {
  std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs;
  for (uint32_t i = 1; i <= 20; ++ i) {
    codecConfigs.emplace(
        i,
        folly::make_unique<CodecConfig>(
            i, CompressionCodecType::LZ4, createBinaryData(i * 1024)));
  }
  for (uint32_t i = 50; i <= 64; ++ i) {
    codecConfigs.emplace(
        i,
        folly::make_unique<CodecConfig>(
            i, CompressionCodecType::LZ4, createBinaryData(i * 1024)));
  }

  CompressionCodecManager codecManager(std::move(codecConfigs));
  auto codecMap = codecManager.getCodecMap();

  for (uint32_t i = 1; i <= 64; ++ i) {
    if (i >= 50) {
      validateCodec(codecMap->get(i));
    } else {
      EXPECT_FALSE(codecMap->get(i));
    }
  }
}

TEST(CompressionCodecManager, missingEnd) {
  std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs;
  for (uint32_t i = 1; i <= 50; ++ i) {
    codecConfigs.emplace(
        i,
        folly::make_unique<CodecConfig>(
            i, CompressionCodecType::LZ4, createBinaryData(i * 1024)));
  }

  CompressionCodecManager codecManager(std::move(codecConfigs));
  auto codecMap = codecManager.getCodecMap();

  for (uint32_t i = 1; i <= 64; ++ i) {
    if (i <= 50) {
      validateCodec(codecMap->get(i));
    } else {
      EXPECT_FALSE(codecMap->get(i));
    }
  }
}

TEST(CompressionCodecManager, invalidDictionary) {
  std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs;
  codecConfigs.emplace(
      1,
      folly::make_unique<CodecConfig>(
          1, CompressionCodecType::LZ4, createBinaryData(10 * 1024)));
  codecConfigs.emplace(
      2,
      folly::make_unique<CodecConfig>(
          2, CompressionCodecType::LZ4, createBinaryData(65 * 1024)));
  codecConfigs.emplace(
      3,
      folly::make_unique<CodecConfig>(
          3, CompressionCodecType::LZ4, createBinaryData(64 * 1024)));

  CompressionCodecManager codecManager(std::move(codecConfigs));
  auto codecMap = codecManager.getCodecMap();

  EXPECT_FALSE(codecMap->get(1));
  EXPECT_FALSE(codecMap->get(2));
  validateCodec(codecMap->get(3));
}

}}}  // facebook::memcache::test
