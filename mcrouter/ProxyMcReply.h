/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/McReplyBase.h"

namespace facebook { namespace memcache {

class McReply;

namespace mcrouter {

class ProxyMcReply : public McReplyBase {
 public:
  template<typename... Args>
  explicit ProxyMcReply(Args&&... args)
    : McReplyBase(std::forward<Args>(args)...) {}
  /* implicit */ ProxyMcReply(McReplyBase reply);
  /**
   * Creates new McReply objects and moves all data into it.
   */
  static McReply moveToMcReply(ProxyMcReply&& proxyMcReply);
};

}}}
