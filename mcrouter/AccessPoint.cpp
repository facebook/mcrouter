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

namespace facebook { namespace memcache { namespace mcrouter {

AccessPoint::AccessPoint(std::string host, std::string port,
                         mc_protocol_t protocol, mc_transport_t transport)
    : host_(std::move(host)),
      port_(std::move(port)) {

  ap_.host = to<nstring_t>(host_);
  ap_.port = to<nstring_t>(port_);
  ap_.protocol = protocol;
  ap_.transport = transport;
}

AccessPoint::AccessPoint(const AccessPoint& other)
    : host_(other.host_),
      port_(other.port_) {

  ap_.host = to<nstring_t>(host_);
  ap_.port = to<nstring_t>(port_);
  ap_.protocol = other.getProtocol();
  ap_.transport = other.getTransport();
}

AccessPoint::AccessPoint(AccessPoint&& other) noexcept
    : host_(std::move(other.host_)),
      port_(std::move(other.port_)) {

  ap_.host = to<nstring_t>(host_);
  ap_.port = to<nstring_t>(port_);
  ap_.protocol = other.getProtocol();
  ap_.transport = other.getTransport();
}

AccessPoint& AccessPoint::operator=(const AccessPoint& other) {
  if (this == &other) {
    return *this = AccessPoint(other);
  }

  host_ = other.host_;
  port_ = other.port_;

  ap_.host = to<nstring_t>(host_);
  ap_.port = to<nstring_t>(port_);
  ap_.protocol = other.getProtocol();
  ap_.transport = other.getTransport();

  return *this;
}

AccessPoint& AccessPoint::operator=(AccessPoint&& other) {
  FBI_ASSERT(this != &other);

  host_ = std::move(other.host_);
  port_ = std::move(other.port_);

  ap_.host = to<nstring_t>(host_);
  ap_.port = to<nstring_t>(port_);
  ap_.protocol = other.getProtocol();
  ap_.transport = other.getTransport();

  return *this;
}

bool AccessPoint::create(folly::StringPiece host_port_protocol,
                         mc_protocol_t default_protocol,
                         mc_transport_t default_transport,
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

  ap.ap_.transport = default_transport;

  // skip ':'
  host_port_protocol.advance(1);
  auto colon = host_port_protocol.find(':');
  if (colon == std::string::npos) {
    // protocol is optional
    ap.port_ = host_port_protocol.str();
    ap.ap_.protocol = default_protocol;
  } else {
    ap.port_ = host_port_protocol.subpiece(0, colon).str();
    host_port_protocol.advance(colon + 1);
    ap.ap_.protocol = mc_string_to_protocol(host_port_protocol.data());
  }

  if (ap.host_.empty() || ap.port_.empty()) {
    return false;
  }

  ap.ap_.host = to<nstring_t>(ap.host_);
  ap.ap_.port = to<nstring_t>(ap.port_);

  return true;
}

std::string AccessPoint::toHostPortString() const {
  try {
    folly::IPAddress ip(host_);
    auto hostPort = ip.toFullyQualified();
    if (ip.isV6()) {
      hostPort = "[" + hostPort + "]";
    }
    return hostPort + ":" + port_;
  } catch (const folly::IPAddressFormatException& e) {
    // host is not IP address (e.g. 'localhost')
    return host_ + ":" + port_;
  }
}

std::string AccessPoint::toString() const {
  assert(ap_.protocol != mc_unknown_protocol);
  return folly::stringPrintf("%s:%s:%s", toHostPortString().data(),
                             (ap_.transport == mc_stream) ? "TCP" : "UDP",
                             mc_protocol_to_string(ap_.protocol));
}

}}}  // facebook::memcache::mcrouter
