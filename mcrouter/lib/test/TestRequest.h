/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequestWithContext.h"

namespace facebook { namespace memcache {

struct Context {
  std::function<void()> onDestroyed;
  explicit Context(std::function<void()> f) : onDestroyed(f) {}
  ~Context() { onDestroyed(); }
};
typedef McRequestWithContext<Context> TestRequest;

template <typename Operation>
struct ReplyType<Operation, TestRequest> {
  typedef class McReply type;
};

}} // facebook::memcache
