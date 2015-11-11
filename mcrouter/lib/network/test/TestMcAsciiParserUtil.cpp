/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TestMcAsciiParserUtil.h"

#include <unordered_map>

#include <folly/io/IOBuf.h>

namespace facebook { namespace memcache {

std::vector<std::vector<size_t>> genChunkedDataSets(size_t length,
                                                    size_t maxPieceSize) {
  if (maxPieceSize == 0) {
    maxPieceSize = length;
  }
  static std::unordered_map<std::pair<size_t, size_t>,
                            std::vector<std::vector<size_t>>> prec;

  auto it = prec.find(std::make_pair(length, maxPieceSize));
  if (it != prec.end()) {
    return it->second;
  }

  std::vector<std::vector<size_t>> splits;
  if (length <= maxPieceSize) {
    splits.push_back({length});
  }

  for (auto piece = 1; piece < std::min(maxPieceSize + 1, length); ++piece) {
    auto vecs = genChunkedDataSets(length - piece, maxPieceSize);
    splits.reserve(splits.size() + vecs.size());
    for (auto& v : vecs) {
      v.emplace_back(piece);
      splits.emplace_back(std::move(v));
    }
  }

  prec[std::make_pair(length, maxPieceSize)] = splits;

  return splits;
}

std::unique_ptr<folly::IOBuf> chunkData(folly::IOBuf data,
                                        const std::vector<size_t>& pieces) {
  data.coalesce();
  size_t start = 0;
  std::unique_ptr<folly::IOBuf> buffer;
  for (const auto& piece : pieces) {
    auto p = folly::IOBuf::copyBuffer(data.data() + start, piece);
    if (start == 0) {
      buffer = std::move(p);
    } else {
      buffer->prependChain(std::move(p));
    }
    start += piece;
  }
  return buffer;
}

}}  // facebook::memcache
