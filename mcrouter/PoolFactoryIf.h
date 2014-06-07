/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>

#include "folly/Range.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyGenericPool;

/**
 * PoolFactory interface to easily mock it.
 */
class PoolFactoryIf {
public:
  virtual std::shared_ptr<ProxyGenericPool>
  fetchPool(folly::StringPiece poolName) = 0;

  /**
   * Parses a single pool from given json blob.
   *
   * @param jpool should be the pool object.
   * @param jpools list of pools for server list link
   */
  virtual std::shared_ptr<ProxyGenericPool>
  parsePool(const std::string& pool_name_str,
            const folly::dynamic& jpool,
            const folly::dynamic& jpools) = 0;

  virtual ~PoolFactoryIf() { }
};

}}} // facebook::memcache::mcrouter
