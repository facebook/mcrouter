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

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <folly/ThreadLocal.h>

#include "mcrouter/lib/Compression.h"

namespace facebook {
namespace memcache {

class CompressionCodecMap;

/**
 * Represents a configuration of a compression codec.
 */
struct CodecConfig {
  const uint32_t id;
  const CompressionCodecType codecType;
  const std::string dictionary;

  CodecConfig(uint32_t codecId, CompressionCodecType type, std::string dic)
      : id(codecId), codecType(type), dictionary(std::move(dic)) {}
};
using CodecConfigPtr = std::unique_ptr<CodecConfig>;


/**
 * Manager of compression codecs.
 */
class CompressionCodecManager {
 public:
  explicit CompressionCodecManager(
      std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs) noexcept;

  /**
   * Return the compression codec map.
   * Note: thread-safe.
   */
  CompressionCodecMap* getCodecMap();

 private:
  // Storage of compression codec configs (codecId -> codecConfig).
  std::unordered_map<uint32_t, CodecConfigPtr> codecConfigs_;
  // ThreadLocal of compression codec map, as codecs are not thread-safe.
  folly::ThreadLocal<CompressionCodecMap> compressionCodecMap_;
  // Codec id range
  uint32_t smallestId_{0};
  uint32_t size_{0};

  CompressionCodecMap* buildCodecMap();
};


/**
 * Map of codec compressors
 * The ids of the codecs held by this map must be contiguous.
 */
class CompressionCodecMap {
 public:
  /**
   * Returns the compression codec of the given id.
   *
   * @param id  Id of the codec.
   * @return    The codec (or nullptr if not found).
   */
  CompressionCodec* get(uint32_t id) const noexcept;

  /**
   * Returns the size of the map.
   */
  size_t size() const noexcept {
    return codecs_.size();
  }

 private:
  std::vector<std::unique_ptr<CompressionCodec>> codecs_;
  const uint32_t firstId_{0};

  /**
   * Builds an empty codec map.
   */
  CompressionCodecMap() noexcept;

  /**
   * Builds a map containing codecs which the ids are within the given range.
   * Note: All codecs in the [smallestId, smallestId + size]
   *       range must be present and valid.
   *
   * @param codecConfigs  Map of (codecId -> codecConfig). Must contain all
   *                      codecs in the given range.
   * @param smallestId    First id of the range of codecs.
   * @param size          Size of the range.
   */
  CompressionCodecMap(
      const std::unordered_map<uint32_t, CodecConfigPtr>& codecConfigs,
      uint32_t smallestId, uint32_t size) noexcept;


  // Return the codecs_ vector index given the codec id.
  uint32_t index(uint32_t id) const noexcept;

  friend class CompressionCodecManager;
};
}  // memcache
}  // facebook
