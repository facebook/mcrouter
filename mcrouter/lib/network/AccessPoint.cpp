/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AccessPoint.h"

#include <folly/Conv.h>
#include <folly/IPAddress.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

AccessPoint::AccessPoint(folly::StringPiece host, uint16_t port,
                         mc_protocol_t protocol)
    : port_(port),
      protocol_(protocol) {

  try {
    folly::IPAddress ip(host);
    host_ = ip.toFullyQualified();
    isV6_ = ip.isV6();
  } catch (const folly::IPAddressFormatException& e) {
    // host is not an IP address (e.g. 'localhost')
    host_ = host.str();
    isV6_ = false;
  }
}

std::shared_ptr<AccessPoint>
AccessPoint::create(folly::StringPiece hostPortProtocol,
                    mc_protocol_t defaultProtocol) {
  if (hostPortProtocol.empty()) {
    return nullptr;
  }

  folly::StringPiece host;
  uint16_t port;
  if (hostPortProtocol[0] == '[') {
    // IPv6
    auto closing = hostPortProtocol.find(']');
    if (closing == std::string::npos) {
      return nullptr;
    }
    host = hostPortProtocol.subpiece(1, closing - 1);
    hostPortProtocol.advance(closing + 1);
  } else {
    // IPv4 or hostname
    auto colon = hostPortProtocol.find(':');
    if (colon == std::string::npos) {
      return nullptr;
    }
    host = hostPortProtocol.subpiece(0, colon);
    hostPortProtocol.advance(colon);
  }

  if (hostPortProtocol.empty() || hostPortProtocol[0] != ':') {
    // port is required
    return nullptr;
  }

  // skip ':'
  hostPortProtocol.advance(1);
  auto colon = hostPortProtocol.find(':');
  if (colon == std::string::npos) {
    // protocol is optional

    if (hostPortProtocol.empty()) {
      return nullptr;
    }
    port = folly::to<uint16_t>(hostPortProtocol);
  } else {
    if (colon == 0) {
      return nullptr;
    }
    port = folly::to<uint16_t>(hostPortProtocol.subpiece(0, colon));
    hostPortProtocol.advance(colon + 1);
    defaultProtocol = mc_string_to_protocol(hostPortProtocol.data());
  }

  if (host.empty()) {
    return nullptr;
  }

  return std::make_shared<AccessPoint>(host, port, defaultProtocol);
}

std::string AccessPoint::toHostPortString() const {
  if (isV6_) {
    return folly::to<std::string>("[", host_, "]:", port_);
  }
  return folly::to<std::string>(host_, ":", port_);
}

std::string AccessPoint::toString() const {
  assert(protocol_ != mc_unknown_protocol);
  return folly::to<std::string>(toHostPortString(), ":TCP:",
                                mc_protocol_to_string(protocol_));
}

}}  // facebook::memcache
