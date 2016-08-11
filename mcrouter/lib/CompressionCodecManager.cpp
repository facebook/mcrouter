/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CompressionCodecManager.h"

#include <algorithm>

#include <folly/Format.h>
#include <folly/io/IOBuf.h>

namespace facebook {
namespace memcache {

/***************************
 * CompressionCodecManager *
 ***************************/
CompressionCodecManager::CompressionCodecManager(
    std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs) noexcept
    : codecConfigs_(std::move(codecConfigs)),
      compressionCodecMap_([this]() { return buildCodecMap(); }) {
  // Validate all dictionaries
  std::vector<uint32_t> badCodecConfigs;
  int64_t largestId = 0;
  for (const auto& it : codecConfigs_) {
    auto codecId = it.first;
    const auto& config = it.second;
    try {
      // createCompressionCodec throws if the dictionary is invalid.
      createCompressionCodec(
          config->codecType,
          folly::IOBuf::wrapBuffer(
              config->dictionary.data(), config->dictionary.size()),
          codecId,
          config->filteringOptions,
          config->compressionLevel);
      largestId = std::max<int64_t>(largestId, codecId);
    } catch (const std::exception& e) {
      badCodecConfigs.push_back(codecId);
      LOG(ERROR) << "Compression codec config [" << codecId << "] is invalid.";
    }
  }
  for (auto id : badCodecConfigs) {
    codecConfigs_.erase(id);
  }

  if (!codecConfigs_.empty()) {
    // Get the longest contiguous range ending in 'largestId'
    smallestId_ = 0;
    for (int64_t i = largestId - 1; i >= 0; --i) {
      const auto& it = codecConfigs_.find(i);
      if (it == codecConfigs_.end()) {
        smallestId_ = i + 1;
        break;
      }
    }
    size_ = largestId - smallestId_ + 1;
    LOG(INFO) << "Using " << size_ << " compression codecs (range: ["
              << smallestId_ << ", " << largestId << "])";
  } else {
    LOG(WARNING) << "No valid compression codec found. Compression disabled.";
  }
}

const CompressionCodecMap* CompressionCodecManager::getCodecMap() const {
  return compressionCodecMap_.get();
}

CompressionCodecMap* CompressionCodecManager::buildCodecMap() {
  if (size_ == 0) {
    return new CompressionCodecMap();
  }
  return new CompressionCodecMap(codecConfigs_, smallestId_, size_);
}


/***********************
 * CompressionCodecMap *
 ***********************/
CompressionCodecMap::CompressionCodecMap() noexcept {
}

CompressionCodecMap::CompressionCodecMap(
    const std::unordered_map<uint32_t, CodecConfigPtr>& codecConfigs,
    uint32_t smallestId, uint32_t size) noexcept
    : firstId_(smallestId) {
  assert(codecConfigs.size() >= size);

  codecs_.resize(size);
  for (uint32_t id = firstId_; id < (firstId_ + size); ++id) {
    const auto& it = codecConfigs.find(id);
    assert(it != codecConfigs.end());
    const auto& config = it->second;
    codecs_[index(id)] = createCompressionCodec(
        config->codecType,
        folly::IOBuf::wrapBuffer(
            config->dictionary.data(), config->dictionary.size()),
        id,
        config->filteringOptions,
        config->compressionLevel);
  }
}

CompressionCodec* CompressionCodecMap::get(uint32_t id) const noexcept {
  if (id < firstId_ || index(id) >= size()) {
    return nullptr;
  }
  return codecs_[index(id)].get();
}

CompressionCodec* CompressionCodecMap::getBest(
    const CodecIdRange& codecRange) const noexcept {
  uint32_t lastId = codecRange.firstId + codecRange.size - 1;
  for (int64_t i = lastId; i >= codecRange.firstId; --i) {
    auto codec = get(i);
    if (codec != nullptr && codec->filteringOptions().isEnabled) {
      return codec;
    }
  }
  return nullptr;
}

uint32_t CompressionCodecMap::index(uint32_t id) const noexcept {
  return id - firstId_;
}
}  // memcache
}  // facebook
