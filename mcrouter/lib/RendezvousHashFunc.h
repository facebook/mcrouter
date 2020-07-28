/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <queue>
#include <vector>

#include <glog/logging.h>

#include <folly/Range.h>

#include "mcrouter/lib/HashFunctionType.h"

namespace facebook {
namespace memcache {

/**
 * An implementation of Rendezvous hashing. For efficiency reasons, we don't
 * hash mixed keys directly. Instead we mix the endpoint hash and key hash.
 *
 * See https://en.wikipedia.org/wiki/Rendezvous_hashing for a detailed
 * description of the algorithm.
 */
class RendezvousHashFunc {
 public:
  /**
   * @param endpoints  A list of backend servers
   */
  explicit RendezvousHashFunc(const std::vector<folly::StringPiece>& endpoints);

  size_t operator()(folly::StringPiece key) const;

  class Iterator {
   public:
    Iterator(const std::vector<uint64_t>& hashes, folly::StringPiece key);
    // An end / empty iterator.
    Iterator() {}

    Iterator& operator++();

    bool operator==(const Iterator& other) const {
      return queue_.size() == other.queue_.size();
    }

    size_t operator*() const {
      CHECK(!queue_.empty());
      return queue_.top().index;
    }

    bool empty() const {
      return queue_.empty();
    }

   private:
    struct ScoreAndIndex {
      bool operator<(const ScoreAndIndex& other) const {
        return score < other.score ||
            (score == other.score && index < other.index);
      }

      uint64_t score;
      size_t index;
    };

    static std::priority_queue<ScoreAndIndex> make_queue(
        const std::vector<uint64_t>& endpointHashes,
        const folly::StringPiece key);

    std::priority_queue<ScoreAndIndex> queue_;
  };

  Iterator begin(folly::StringPiece key) const {
    return Iterator(endpointHashes_, key);
  }

  Iterator end() const {
    return Iterator();
  }

  static const char* type() {
    return "Rendezvous";
  }

  static HashFunctionType typeId() {
    return HashFunctionType::Rendezvous;
  }

 private:
  std::vector<uint64_t> endpointHashes_;
};
} // namespace memcache
} // namespace facebook
