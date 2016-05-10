/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <limits.h>

#include <folly/Bits.h>
#include <folly/Portability.h>
#include <folly/SocketAddress.h>

namespace facebook {
namespace memcache {

/**
 * Represents the direction of a ConnectionFifo message.
 */
enum class MessageDirection : uint8_t {
  Sent = 0,
  Received = 1
};

/**
 * Header of the message of ConnectionFifo.
 */
struct FOLLY_PACK_ATTR MessageHeader {
 public:
  constexpr static size_t kIpAddressMaxSize = 40;

  uint32_t magic() const {
    return folly::Endian::little(magic_);
  }
  uint8_t version() const {
    return version_;
  }
  const char* peerIpAddress() const {
    return peerIpAddress_;
  }
  uint16_t peerPort() const {
    return folly::Endian::little(peerPort_);
  }
  uint64_t connectionId() const {
    return folly::Endian::little(connectionId_);
  }
  uint16_t localPort() const {
    return folly::Endian::little(localPort_);
  }
  MessageDirection direction() const {
    return direction_;
  }

  char* peerIpAddressModifiable() {
    return peerIpAddress_;
  }
  void setPeerPort(uint16_t val) {
    peerPort_ = folly::Endian::little(val);
  }
  void setConnectionId(uint64_t val) {
    connectionId_ = folly::Endian::little(val);
  }
  void setLocalPort(uint16_t val) {
    localPort_ = folly::Endian::little(val);
  }
  void setDirection(MessageDirection val) {
    direction_ = val;
  }

  folly::SocketAddress getLocalAddress();
  folly::SocketAddress getPeerAddress();

  static size_t size(uint8_t v);

 private:
  // Control fields
  uint32_t magic_ = folly::Endian::little<uint32_t>(0xfaceb00c);
  uint8_t version_{2};

  // Peer address fields
  char peerIpAddress_[kIpAddressMaxSize]{'\0'}; // 0-terminated string of ip
  uint16_t peerPort_{0};

  // Message fields
  uint64_t connectionId_{0};

  // Local address fields
  uint16_t localPort_{0};

  // Direction of the message sent
  MessageDirection direction_{MessageDirection::Sent};
};

/**
 * Header of the packet.
 * FIFO's can only write up to PIPE_BUF (tipically 4096 in linux) bytes
 * atomically at a time. For that reason, calls to Fifo::writeIfConnected()
 * are broke down into packets.
 */
struct FOLLY_PACK_ATTR PacketHeader {
 public:
  uint64_t connectionId() const {
    return folly::Endian::little(connectionId_);
  }
  uint32_t packetSize() const {
    return folly::Endian::little(packetSize_);
  }
  uint32_t packetId() const {
    return folly::Endian::little(packetId_);
  }
  void setConnectionId(uint64_t val) {
    connectionId_ = folly::Endian::little(val);
  }
  void setPacketSize(uint32_t val) {
    packetSize_ = folly::Endian::little(val);
  }
  void setPacketId(uint32_t val) {
    packetId_ = folly::Endian::little(val);
  }

 private:
  uint64_t connectionId_{0};
  uint32_t packetSize_{0};
  uint32_t packetId_{0};
};
constexpr uint32_t kFifoMaxPacketSize = PIPE_BUF - sizeof(PacketHeader);
static_assert(PIPE_BUF > sizeof(MessageHeader) + sizeof(PacketHeader),
              "sizeof(PacketHeader) + sizeof(MessageHeader) "
              "must be smaller than PIPE_BUF.");

} // memcache
} // facebook
