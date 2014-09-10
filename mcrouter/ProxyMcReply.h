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

class McReply;

namespace mcrouter {

class ProxyClientCommon;

class ProxyMcReply : public McReplyBase {
 public:
  template<typename... Args>
  explicit ProxyMcReply(Args&&... args)
    : McReplyBase(std::forward<Args>(args)...) {}
  /* implicit */ ProxyMcReply(McReplyBase reply);
  void setDestination(std::shared_ptr<const ProxyClientCommon> dest);
  /**
   * Returns the destination that this reply was received from.
   * The value is only set when an error reply was received.
   */
  std::shared_ptr<const ProxyClientCommon> getDestination() const;

  /**
   * Creates new McReply objects and moves all data into it.
   */
  static McReply moveToMcReply(ProxyMcReply&& proxyMcReply);

 private:
  std::shared_ptr<const ProxyClientCommon> dest_;
};

}}}
