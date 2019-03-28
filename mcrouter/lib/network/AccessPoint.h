/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <memory>
#include <string>

#include <folly/Range.h>

#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/SecurityOptions.h"

namespace facebook {
namespace memcache {

struct AccessPoint {
  explicit AccessPoint(
      folly::StringPiece host = "",
      uint16_t port = 0,
      mc_protocol_t protocol = mc_unknown_protocol,
      SecurityMech mech = SecurityMech::NONE,
      bool compressed = false,
      bool unixDomainSocket = false);

  /**
   * @param apString accepts host:port, host:port:protocol and
   *                 host:port:protocol:(ssl|plain):(compressed|notcompressed)
   * @param defaultProtocol this is the protocol used if no protocol specified
   * @param defaultUseSsl this is the protocol used if no protocol specified
   * @param portOverride This overrides the port. If 0, port from
   *                     hostPortProtocol used
   * @param defaultCompressed The is the compression config to use if it's not
   *                          specified in the string.
   *
   * @return shared_ptr to an AccessPoint object
   */
  static std::shared_ptr<AccessPoint> create(
      folly::StringPiece apString,
      mc_protocol_t defaultProtocol,
      SecurityMech defaultMech = SecurityMech::NONE,
      uint16_t portOverride = 0,
      bool defaultCompressed = false);

  const std::string& getHost() const {
    return host_;
  }

  uint16_t getPort() const {
    return port_;
  }

  mc_protocol_t getProtocol() const {
    return protocol_;
  }

  SecurityMech getSecurityMech() const {
    return securityMech_;
  }

  bool useSsl() const {
    return securityMech_ != SecurityMech::NONE;
  }

  bool compressed() const {
    return compressed_;
  }

  bool isUnixDomainSocket() const {
    return unixDomainSocket_;
  }

  /**
   * @return [host]:port if address is IPv6, host:port otherwise
   */
  std::string toHostPortString() const;

  /**
   * @return HostPort:protocol:(ssl|plain) string
   */
  std::string toString() const;

  void disableCompression();

  void setSecurityMech(SecurityMech mech) {
    securityMech_ = mech;
  }

  void setPort(uint16_t port) {
    port_ = port;
  }

 private:
  std::string host_;
  uint16_t port_;
  mc_protocol_t protocol_ : 8;
  SecurityMech securityMech_{SecurityMech::NONE};
  bool compressed_{false};
  bool isV6_{false};
  bool unixDomainSocket_{false};
};

} // memcache
} // facebook
