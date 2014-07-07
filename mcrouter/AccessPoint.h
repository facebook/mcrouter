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

#include "mcrouter/lib/mc/protocol.h"

namespace facebook { namespace memcache { namespace mcrouter {

struct AccessPoint {
  explicit AccessPoint(std::string host = "",
                       std::string port = "",
                       mc_protocol_t protocol = mc_unknown_protocol,
                       mc_transport_t transport = mc_unknown_transport);

  AccessPoint(const AccessPoint& other);

  AccessPoint(AccessPoint&& other) noexcept;

  AccessPoint& operator=(const AccessPoint& other);

  AccessPoint& operator=(AccessPoint&& other);

  static bool create(const std::string& host_port_protocol,
                     mc_protocol_t default_protocol,
                     mc_transport_t default_transport,
                     AccessPoint& ap);

  const mc_accesspoint_t& mc_accesspoint() const {
    return ap_;
  }

  std::string getHost() const {
    return host_;
  }

  std::string getPort() const {
    return port_;
  }

  mc_protocol_t getProtocol() const {
    return ap_.protocol;
  }

  mc_transport_t getTransport() const {
    return ap_.transport;
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
  mc_accesspoint_t ap_;
  std::string host_;
  std::string port_;
};

}}}  // facebook::memcache::mcrouter
