/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/config-impl.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestinationMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class Operation>
int ProxyDestination::send(const McRequest &request, Operation, void* req_ctx,
                           uint64_t senderId) {
  FBI_ASSERT(proxy->magic == proxy_magic);

  proxy->destinationMap->markAsActive(*this);

  return client_->send(request, Operation(), req_ctx, senderId);
}

}}}  // facebook::memcache::mcrouter
