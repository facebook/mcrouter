/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>

#include "mcrouter/tools/mcpiper/ValueFormatter.h"

namespace facebook {
namespace memcache {

class CompressionCodecMap;
class MessagePrinter;
template <class T>
class SnifferParserBase;

namespace detail {

template <class Reply>
struct MatchingRequest {
  static constexpr const char* name();
};

} // detail

/**
 * Returns the default fifo root.
 */
std::string getDefaultFifoRoot();

/**
 * Creates value formatter.
 */
std::unique_ptr<ValueFormatter> createValueFormatter();

/**
 * Return current version.
 */
std::string getVersion();

/**
 * Initializes compression support.
 */
bool initCompression();

/**
 * Gets compression codec map.
 * If compression is not initialized, return nullptr.
 */
const CompressionCodecMap* getCompressionCodecMap();

/**
 * Adds SnifferParser based on protocol to the parser map
 */
std::unordered_map<
    uint64_t,
    std::unique_ptr<SnifferParserBase<MessagePrinter>>>::iterator
addCarbonSnifferParser(
    std::string name,
    std::unordered_map<
        uint64_t,
        std::unique_ptr<SnifferParserBase<MessagePrinter>>>& parserMap,
    uint64_t connectionId,
    MessagePrinter& printer);
}
} // facebook::memcache
