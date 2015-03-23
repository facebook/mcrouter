/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

#include <folly/Range.h>

#include "mcrouter/lib/mc/protocol.h"

namespace facebook { namespace memcache {

struct AccessPoint {
  explicit AccessPoint(folly::StringPiece host = "",
                       uint16_t port = 0,
                       mc_protocol_t protocol = mc_unknown_protocol);

  static bool create(folly::StringPiece host_port_protocol,
                     mc_protocol_t default_protocol,
                     AccessPoint& ap);

  const folly::StringPiece getHost() const {
    return host_;
  }

  uint16_t getPort() const {
    return port_;
  }

  mc_protocol_t getProtocol() const {
    return protocol_;
  }

  /**
   * @return [host]:port if address is IPv6, host:port otherwise
   */
  std::string toHostPortString() const;

  /**
   * @return HostPort:transport:protocol string
   */
  std::string toString() const;

 private:
  std::string host_;
  uint16_t port_;
  mc_protocol_t protocol_;
  bool isV6_;

  void initialize();
};

}}  // facebook::memcache
