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
   * @param msgId   Identifier of this message.
   * @param iov     Data of the message, to write to the pipe.
   * @param iovcnt  Size of iov.
   * @return        True if the data was written. False otherwise.
   */
  bool writeIfConnected(uint64_t msgId,
                        const struct iovec* iov,
                        size_t iovcnt) noexcept;
  bool writeIfConnected(uint64_t msgId, void* buf, size_t len) noexcept;

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
 * Header of the packet.
 * FIFO's can only write up to PIPE_BUF (tipically 4096 in linux) bytes
 * atomically at a time. For that reason, calls to Fifo::writeIfConnected()
 * are broke down into packets.
 */
struct PacketHeader {
  uint64_t msgId;
  uint32_t packetSize;
  uint32_t packetId;
};
static_assert(PIPE_BUF > sizeof(PacketHeader),
              "sizeof(PacketHeader) must be smaller than PIPE_BUF.");

}} // facebook::memcache
