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

#include <stdint.h>

namespace facebook { namespace memcache {

class ListenSocket {
 public:
  /**
   * @throws std::runtime_error  if failed to create a listen socket
   */
  ListenSocket();
  ~ListenSocket();

  uint16_t getPort() const {
    return port_;
  }

  int getSocketFd() const {
    return socketFd_;
  }

  // movable, but not copyable
  ListenSocket(ListenSocket&& other) noexcept;
  ListenSocket& operator=(ListenSocket&& other) noexcept;
  ListenSocket(const ListenSocket&) = delete;
  ListenSocket& operator=(const ListenSocket&) = delete;
 private:
  int socketFd_{-1};
  uint16_t port_{0};
};

/**
 * @return true  if port is open, false otherwise
 */
bool isPortOpen(uint16_t port);

}}  // facebook::memcache
