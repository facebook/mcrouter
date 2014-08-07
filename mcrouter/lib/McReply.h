/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/McReplyBase.h"

namespace facebook { namespace memcache {

/**
 * mc_msg_t-based Reply implementation.
 */
class McReply : public McReplyBase {
 public:
  template<typename... Args>
  explicit McReply(Args&&... args)
    : McReplyBase(std::forward<Args>(args)...) {}
};

}}  // facebook::memcache
