/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/config-impl.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <typename Operation>
void ProxyRequestContext::onReplyReceived(
  const ProxyClientCommon& pclient,
  const ProxyMcRequest& request,
  const ProxyMcReply& reply,
  const int64_t startTimeUs,
  const int64_t endTimeUs,
  Operation) {

  logger_->log(pclient, request, reply, startTimeUs, endTimeUs, Operation());
}

}}} // facebook::memcache::mcrouter
