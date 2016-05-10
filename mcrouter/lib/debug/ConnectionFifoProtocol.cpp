/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ConnectionFifoProtocol.h"

namespace facebook {
namespace memcache {

folly::SocketAddress MessageHeader::getLocalAddress() {
  folly::SocketAddress address;

  if (version() < 2) {
    return address;
  }

  try {
    address.setFromLocalPort(localPort());
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error parsing address: " << ex.what();
  }

  return address;
}

folly::SocketAddress MessageHeader::getPeerAddress() {
  folly::SocketAddress address;

  if (peerIpAddress()[0] == '\0') {
    return address;
  }

  try {
    address.setFromIpPort(peerIpAddress(), peerPort());
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error parsing address: " << ex.what();
  }

  return address;
}

/* static */ size_t MessageHeader::size(uint8_t v) {
  switch (v) {
    case 1:
      return sizeof(MessageHeader) - sizeof(localPort_) - sizeof(direction_);
    case 2:
      return sizeof(MessageHeader);
    default:
      throw std::logic_error(folly::sformat("Invalid version {}", v));
  }
}

} // memcache
} // facebook
