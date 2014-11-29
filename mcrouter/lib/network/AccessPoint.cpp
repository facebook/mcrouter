/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "AccessPoint.h"

#include <folly/IPAddress.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

AccessPoint::AccessPoint(folly::StringPiece host, uint16_t port,
                         mc_protocol_t protocol)
    : host_(host.str()),
      port_(port),
      protocol_(protocol) {
}

bool AccessPoint::create(folly::StringPiece host_port_protocol,
                         mc_protocol_t default_protocol,
                         AccessPoint& ap) {
  if (host_port_protocol.empty()) {
    return false;
  }

  if (host_port_protocol[0] == '[') {
    // IPv6
    auto closing = host_port_protocol.find(']');
    if (closing == std::string::npos) {
      return false;
    }
    ap.host_ = host_port_protocol.subpiece(1, closing - 1).str();
    host_port_protocol.advance(closing + 1);
  } else {
    // IPv4 or hostname
    auto colon = host_port_protocol.find(':');
    if (colon == std::string::npos) {
      return false;
    }
    ap.host_ = host_port_protocol.subpiece(0, colon).str();
    host_port_protocol.advance(colon);
  }

  if (host_port_protocol.empty() || host_port_protocol[0] != ':') {
    // port is required
    return false;
  }

  // skip ':'
  host_port_protocol.advance(1);
  auto colon = host_port_protocol.find(':');
  if (colon == std::string::npos) {
    // protocol is optional

    if (host_port_protocol.empty()) {
      return false;
    }
    ap.port_ = folly::to<uint16_t>(host_port_protocol);
    ap.protocol_ = default_protocol;
  } else {
    if (colon == 0) {
      return false;
    }
    ap.port_ = folly::to<uint16_t>(host_port_protocol.subpiece(0, colon));
    host_port_protocol.advance(colon + 1);
    ap.protocol_ = mc_string_to_protocol(host_port_protocol.data());
  }

  if (ap.host_.empty()) {
    return false;
  }

  return true;
}

std::string AccessPoint::toHostPortString() const {
  try {
    folly::IPAddress ip(host_);
    auto hostPort = ip.toFullyQualified();
    if (ip.isV6()) {
      hostPort = "[" + hostPort + "]";
    }
    return hostPort + ":" + folly::to<std::string>(port_);
  } catch (const folly::IPAddressFormatException& e) {
    // host is not IP address (e.g. 'localhost')
    return host_ + ":" + folly::to<std::string>(port_);
  }
}

std::string AccessPoint::toString() const {
  assert(protocol_ != mc_unknown_protocol);
  return folly::stringPrintf("%s:TCP:%s", toHostPortString().data(),
                             mc_protocol_to_string(protocol_));
}

}}  // facebook::memcache
