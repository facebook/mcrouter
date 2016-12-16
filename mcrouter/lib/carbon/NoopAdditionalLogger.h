/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

// Forward declaration
namespace facebook {
namespace memcache {
namespace mcrouter {
class ProxyBase;
struct RequestLoggerContext;
} // mcrouter
} // memcache
} // facebook

namespace carbon {

class NoopAdditionalLogger {
 public:
  explicit NoopAdditionalLogger(facebook::memcache::mcrouter::ProxyBase*) {}

  template <class Request>
  void log(
      const Request&,
      const typename Request::reply_type&,
      const facebook::memcache::mcrouter::RequestLoggerContext&) {}
};

} // carbon
