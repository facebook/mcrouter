/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MessagePrinter.h"

namespace facebook { namespace memcache {

namespace {

bool matchIPAddress(const folly::IPAddress& expectedIp,
                    const folly::SocketAddress& address) {
  return !address.empty() && expectedIp == address.getIPAddress();
}

bool matchPort(uint16_t expectedPort, const folly::SocketAddress& address) {
  return !address.empty() && expectedPort == address.getPort();
}

} // anonymous namespace

MessagePrinter::MessagePrinter(Options options,
                               Filter filter,
                               std::unique_ptr<ValueFormatter> valueFormatter)
    : options_(std::move(options)),
      filter_(std::move(filter)),
      valueFormatter_(std::move(valueFormatter)) {
  if (options_.disableColor) {
    targetOut_.setColorOutput(false);
  }
}

bool MessagePrinter::matchAddress(const folly::SocketAddress& from,
                                  const folly::SocketAddress& to) const {
  // Initial filters
  if (!filter_.host.empty() &&
      !matchIPAddress(filter_.host, from) &&
      !matchIPAddress(filter_.host, to)) {
    return false;
  }
  if (filter_.port != 0 &&
      !matchPort(filter_.port, from) &&
      !matchPort(filter_.port, to)) {
    return false;
  }

  return true;
}

std::string MessagePrinter::serializeAddresses(const folly::SocketAddress& from,
                                               const folly::SocketAddress& to) {
  std::string out;

  if (!from.empty()) {
    out.append(from.describe());
  }
  if (!from.empty() || !to.empty()) {
    out.append(" -> ");
  }
  if (!to.empty()) {
    out.append(to.describe());
  }

  return out;
}

std::string MessagePrinter::serializeMessageHeader(mc_op_t op,
                                                   mc_res_t result,
                                                   const std::string& key) {
  std::string out;

  if (op != mc_op_unknown) {
    out.append(mc_op_to_string(op));
  }
  if (result != mc_res_unknown) {
    if (out.size() > 0) {
      out.push_back(' ');
    }
    out.append(mc_res_to_string(result));
  }
  if (key.size()) {
    if (out.size() > 0) {
      out.push_back(' ');
    }
    out.append(folly::backslashify(key));
  }

  return out;
}

/**
 * Matches all the occurences of "pattern" in "text"
 *
 * @return A vector of pairs containing the index and size (respectively)
 *         of all ocurrences.
 */
std::vector<std::pair<size_t, size_t>> MessagePrinter::matchAll(
    folly::StringPiece text, const boost::regex& pattern) const {
  std::vector<std::pair<size_t, size_t>> result;

  boost::cregex_token_iterator it(text.begin(), text.end(), pattern);
  boost::cregex_token_iterator end;
  while (it != end) {
    result.emplace_back(it->first - text.begin(), it->length());
    ++it;
  }
  return result;
}

}} // facebook::memcache
