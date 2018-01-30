/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdint.h>

#include <chrono>
#include <string>

#include <folly/Range.h>

namespace facebook {
namespace memcache {

class ClientSocket {
 public:
  static constexpr size_t kMaxReplySize = 1000;

  /**
   * @throws std::runtime_error  if failed to create a socket and connect
   */
  explicit ClientSocket(uint16_t port);
  ~ClientSocket();

  /**
   * Write data to socket.
   *
   * @param data  data to write
   * @param timeout  max time to wait for write
   *
   * @throws std::runtime_error  if failed to write data
   */
  void write(
      folly::StringPiece data,
      std::chrono::milliseconds timeout = std::chrono::seconds(1));

  /**
   * Send a request and receive a reply of `replySize`.
   *
   * @param request  string to send
   * @param timeout  max time to wait for send/receive
   *
   * @throws std::runtime_error  if failed to send/receive data
   */
  std::string sendRequest(
      folly::StringPiece request,
      size_t replySize,
      std::chrono::milliseconds timeout = std::chrono::seconds(1));
  std::string sendRequest(
      folly::StringPiece request,
      std::chrono::milliseconds timeout = std::chrono::seconds(1));

  // movable, but not copyable
  ClientSocket(ClientSocket&& other) noexcept;
  ClientSocket& operator=(ClientSocket&& other) noexcept;
  ClientSocket(const ClientSocket&) = delete;
  ClientSocket& operator=(const ClientSocket&) = delete;

 private:
  int socketFd_{-1};
};
}
} // facebook::memcache
