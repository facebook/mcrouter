/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyMcReply.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyMcReply::ProxyMcReply(McReplyBase reply)
  : McReplyBase(std::move(reply)) {}

void ProxyMcReply::setDestination(
    std::shared_ptr<const ProxyClientCommon> dest) {
  dest_ = std::move(dest);
}

std::shared_ptr<const ProxyClientCommon> ProxyMcReply::getDestination() const {
  return dest_;
}

}}}
