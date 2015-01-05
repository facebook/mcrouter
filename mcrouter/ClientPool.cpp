/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ClientPool.h"

#include <folly/dynamic.h>
#include <folly/Memory.h>

#include "mcrouter/ProxyClientCommon.h"

namespace facebook { namespace memcache { namespace mcrouter {

ClientPool::ClientPool(std::string name)
  : name_(std::move(name)) {
}

void ClientPool::setWeights(folly::dynamic weights) {
  weights_ = folly::make_unique<folly::dynamic>(std::move(weights));
}

folly::dynamic* ClientPool::getWeights() const  {
  return weights_.get();
}

}}}  // facebook::memcache::mcrouter
