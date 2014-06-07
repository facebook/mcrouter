/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <string>
#include <vector>

using asox_timer_t = void*;

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;
class stat_t;

class ProxyLogger {
 public:
  explicit ProxyLogger(proxy_t* proxy);
  std::vector<stat_t> log();

  ~ProxyLogger();
 protected:
  proxy_t* proxy_;
 private:
  /**
   * File paths of stats we want to touch and keep their mtimes up-to-date
   */
  std::vector<std::string> touchStatsFilepaths_;

  asox_timer_t statsLoggingTimer_{nullptr};
};

}}}  // facebook::memcache::mcrouter
