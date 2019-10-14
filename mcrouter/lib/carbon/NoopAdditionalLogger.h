/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

// Forward declaration
namespace facebook {
namespace memcache {
namespace mcrouter {
class ProxyRequestContext;
struct RequestLoggerContext;
} // mcrouter
} // memcache
} // facebook

namespace carbon {

class NoopAdditionalLogger {
 public:
  explicit NoopAdditionalLogger(
      const facebook::memcache::mcrouter::ProxyRequestContext&) {}

  template <class Request>
  void logBeforeRequestSent(
      const Request&,
      const facebook::memcache::mcrouter::RequestLoggerContext&) {}

  template <class Request>
  void log(
      const Request&,
      const typename Request::reply_type&,
      const facebook::memcache::mcrouter::RequestLoggerContext&) {}
};

} // carbon
