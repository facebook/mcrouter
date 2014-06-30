/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "AccessPoint.h"

#include <boost/regex.hpp>

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

bool AccessPoint::create(const std::string& host_port_protocol,
                         mc_protocol_t default_protocol,
                         mc_transport_t default_transport,
                         AccessPoint& ap) {

  boost::regex r(
      // host is required but square brackets are optional
      "^(?:\\[)?([-0-9a-zA-Z.:]+)(?:\\])?"
      //port, required
      ":([0-9]+)"
      //protocol, optional
      "(?::([0-9a-zA-Z]+))?$");

  std::string protocol;
  boost::cmatch matches;
  if (!boost::regex_match(host_port_protocol.data(), matches, r)) {
    LOG(ERROR) << "Expected host:port(:protocol)?, got " <<
                  host_port_protocol;
    return false;
  }

  ap.host_ = matches[1].str();
  ap.port_ = matches[2].str();
  if (matches.size() > 3) {
    protocol = matches[3].str();
  }

  ap.ap_.transport = default_transport;

  ap.ap_.protocol = (!protocol.empty())
      ? mc_string_to_protocol(protocol.data())
      : default_protocol;

  ap.ap_.host = to<nstring_t>(ap.host_);
  ap.ap_.port = to<nstring_t>(ap.port_);

  return true;
}

std::string AccessPoint::toString() const {
  FBI_ASSERT(ap_.protocol != mc_unknown_protocol);
  return folly::stringPrintf("%s:%s:%s:%s", host_.data(), port_.data(),
                             (ap_.transport == mc_stream) ? "TCP" : "UDP",
                             mc_protocol_to_string(ap_.protocol));
}

}}}  // facebook::memcache::mcrouter
