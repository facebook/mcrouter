/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <atomic>
#include <limits.h>
#include <string>
#include <sys/uio.h>

#include <folly/Bits.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/Portability.h>

namespace facebook { namespace memcache {

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
   * @param iov         Data of the message, to write to the pipe.
   * @param iovcnt      Size of iov.
   * @return            True if the data was written. False otherwise.
   */
  bool writeIfConnected(const folly::AsyncTransportWrapper* transport,
                        const struct iovec* iov,
                        size_t iovcnt) noexcept;
  bool writeIfConnected(const folly::AsyncTransportWrapper* transport,
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
    return folly::Endian::little(magicLE_);
  }
  uint8_t version() const {
    return version_;
  }
  const char* ipAddress() const {
    return ipAddress_;
  }
  uint16_t port() const {
    return folly::Endian::little(portLE_);
  }
  uint64_t msgId() const {
    return folly::Endian::little(msgIdLE_);
  }

  char* ipAddressModifiable() {
    return ipAddress_;
  }
  void setPort(uint16_t val) {
    portLE_ = folly::Endian::little(val);
  }
  void setMsgId(uint64_t val) {
    msgIdLE_ = folly::Endian::little(val);
  }

 private:
  // Control fields
  const uint32_t magicLE_ = folly::Endian::little<uint32_t>(0xfaceb00c);
  const uint8_t version_ = 1;

  // Address fields
  char ipAddress_[kIpAddressMaxSize]{'\0'}; // 0-terminated string of ip
  uint16_t portLE_ = 0;

  // Message fields
  uint64_t msgIdLE_{0};
};

/**
 * Header of the packet.
 * FIFO's can only write up to PIPE_BUF (tipically 4096 in linux) bytes
 * atomically at a time. For that reason, calls to Fifo::writeIfConnected()
 * are broke down into packets.
 */
struct FOLLY_PACK_ATTR PacketHeader {
 private:
  uint64_t msgIdLE_{0};
  uint32_t packetSizeLE_{0};
  uint32_t packetIdLE_{0};
 public:
  uint64_t msgId() const {
    return folly::Endian::little(msgIdLE_);
  }
  uint32_t packetSize() const {
    return folly::Endian::little(packetSizeLE_);
  }
  uint32_t packetId() const {
    return folly::Endian::little(packetIdLE_);
  }
  void setMsgId(uint64_t val) {
    msgIdLE_ = folly::Endian::little(val);
  }
  void setPacketSize(uint32_t val) {
    packetSizeLE_ = folly::Endian::little(val);
  }
  void setPacketId(uint32_t val) {
    packetIdLE_ = folly::Endian::little(val);
  }
};
constexpr uint32_t kFifoMaxPacketSize = PIPE_BUF - sizeof(PacketHeader);
static_assert(PIPE_BUF > sizeof(MessageHeader) + sizeof(PacketHeader),
              "sizeof(PacketHeader) + sizeof(MessageHeader) "
              "must be smaller than PIPE_BUF.");

}} // facebook::memcache
