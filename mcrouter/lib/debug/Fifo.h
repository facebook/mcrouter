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
#include <sys/uio.h>

#include <atomic>
#include <string>

#include <folly/Bits.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/Portability.h>

namespace facebook { namespace memcache {

/**
 * Represents the direction of a message.
 */
enum class MessageDirection : uint8_t {
  Sent = 0,
  Received = 1
};

/**
 * Writes data to a named pipe (fifo) for debugging purposes.
 *
 * Notes:
 *  - Unless specified otherwise, methods of this class are thread-safe.
 *  - Life of Fifo is managed by FifoManager.
 */
class Fifo {
 public:
  ~Fifo();

  // non copyable
  Fifo(const Fifo& other) = delete;
  Fifo& operator=(Fifo&) = delete;

  // non movable
  Fifo(Fifo&&) noexcept = delete;
  Fifo& operator=(Fifo&&) noexcept = delete;

  /**
   * Tries to connect to the fifo (if not already connected).
   * Note: This method is not thread-safe.
   *
   * @return  True if the pipe is already connected or connection
   *          was established successfully. False otherwise.
   */
  bool tryConnect() noexcept;

  /**
   * Writes data to the FIFO, if there is reader connected to it.
   * Note: Writes are best effort. If, for example, the pipe is full, this
   * method will fail (return false).
   *
   * @param transport   Transport from which data are being mirrored.
   * @param direction   Whether the data was received or sent by transport.
   * @param iov         Data of the message, to write to the pipe.
   * @param iovcnt      Size of iov.
   * @return            True if the data was written. False otherwise.
   */
  bool writeIfConnected(const folly::AsyncTransportWrapper* transport,
                        MessageDirection direction,
                        const struct iovec* iov,
                        size_t iovcnt) noexcept;
  bool writeIfConnected(const folly::AsyncTransportWrapper* transport,
                        MessageDirection direction,
                        void* buf, size_t len) noexcept;

 private:
  explicit Fifo(std::string path);

  // Path of the fifo
  const std::string path_;
  // Fifo file descriptor.
  std::atomic<int> fd_{-1};

  /**
   * Disconnects the pipe.
   */
  void disconnect() noexcept;
  /**
   * Tells whether this fifo is connectted.
   */
  bool isConnected() const noexcept;

  friend class FifoManager;
};

/**
 * Header of the message.
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
  uint64_t msgId() const {
    return folly::Endian::little(msgId_);
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
  void setMsgId(uint64_t val) {
    msgId_ = folly::Endian::little(val);
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
  const uint32_t magic_ = folly::Endian::little<uint32_t>(0xfaceb00c);
  const uint8_t version_{2};

  // Peer address fields
  char peerIpAddress_[kIpAddressMaxSize]{'\0'}; // 0-terminated string of ip
  uint16_t peerPort_{0};

  // Message fields
  uint64_t msgId_{0};

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
  uint64_t msgId() const {
    return folly::Endian::little(msgId_);
  }
  uint32_t packetSize() const {
    return folly::Endian::little(packetSize_);
  }
  uint32_t packetId() const {
    return folly::Endian::little(packetId_);
  }
  void setMsgId(uint64_t val) {
    msgId_ = folly::Endian::little(val);
  }
  void setPacketSize(uint32_t val) {
    packetSize_ = folly::Endian::little(val);
  }
  void setPacketId(uint32_t val) {
    packetId_ = folly::Endian::little(val);
  }

 private:
  uint64_t msgId_{0};
  uint32_t packetSize_{0};
  uint32_t packetId_{0};
};
constexpr uint32_t kFifoMaxPacketSize = PIPE_BUF - sizeof(PacketHeader);
static_assert(PIPE_BUF > sizeof(MessageHeader) + sizeof(PacketHeader),
              "sizeof(PacketHeader) + sizeof(MessageHeader) "
              "must be smaller than PIPE_BUF.");

}} // facebook::memcache
