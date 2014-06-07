/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache { namespace mcrouter {

template<typename F>
void ProxyClientOwner::foreach_shared_synchronized(const F& func) {
  std::lock_guard<std::mutex> lock(mx);
  for (auto& it : pclient_shared) {
    if (auto pcs = it.second.lock()) {
      func(it.first, *pcs);
    }
  }
}

}}}
