/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Range.h>

#include "mcrouter/lib/mc/protocol.h"

namespace carbon {
namespace detail {

template <class Request, class = bool&>
struct HasFailover : public std::false_type {};
template <class Request>
struct HasFailover<Request, decltype(std::declval<Request>().failover())>
    : public std::true_type {};

template <class Request>
typename std::enable_if<HasFailover<Request>::value, void>::type
setRequestFailover(Request& req) {
  req.failover() = true;
}

template <class Request>
typename std::enable_if<!HasFailover<Request>::value, void>::type
setRequestFailover(Request& req) {
  if (!req.key().hasHashStop()) {
    return;
  }
  constexpr folly::StringPiece kFailoverTag = ":failover=1";
  auto keyWithFailover =
      folly::to<std::string>(req.key().fullKey(), kFailoverTag);
  /* It's always safe to not append a failover tag */
  if (keyWithFailover.size() <= MC_KEY_MAX_LEN) {
    req.key() = std::move(keyWithFailover);
  }
}

} // namespace detail
} // namespace carbon
