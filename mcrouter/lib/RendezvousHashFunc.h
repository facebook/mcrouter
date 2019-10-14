/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <vector>

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
