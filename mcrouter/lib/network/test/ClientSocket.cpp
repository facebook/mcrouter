/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ClientSocket.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>

#include <chrono>
#include <thread>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

ClientSocket::ClientSocket(uint16_t port) {
  struct addrinfo hints;
  struct addrinfo* res;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  auto portStr = folly::to<std::string>(port);
  auto ret = ::getaddrinfo("localhost", portStr.data(), &hints, &res);
  checkRuntime(!ret, "Failed to find a local IP: {}", ::gai_strerror(ret));
  SCOPE_EXIT {
    ::freeaddrinfo(res);
  };
  socketFd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (socketFd_ < 0) {
    throwRuntime("Failed to create a socket for port {}: {}",
                 port, folly::errnoStr(errno));
  }

  if (::connect(socketFd_, res->ai_addr, res->ai_addrlen) != 0) {
    auto errStr = folly::errnoStr(errno);
    ::close(socketFd_);
    throwRuntime("Failed to connect to port {}: {}", port, errStr);
  }
}

ClientSocket::ClientSocket(ClientSocket&& other) noexcept
    : socketFd_(other.socketFd_) {
  other.socketFd_ = -1;
}

ClientSocket& ClientSocket::operator=(ClientSocket&& other) noexcept {
  if (this != &other) {
    if (socketFd_ >= 0) {
      ::close(socketFd_);
    }
    socketFd_ = other.socketFd_;
    other.socketFd_ = -1;
  }
  return *this;
}

ClientSocket::~ClientSocket() {
  if (socketFd_ >= 0) {
    ::close(socketFd_);
  }
}

void ClientSocket::write(folly::StringPiece data,
                         std::chrono::milliseconds timeout) {
  size_t written = 0;
  auto timeoutMs = static_cast<size_t>(timeout.count());
  for (size_t i = 0; written < data.size() && i <= timeoutMs; ++i) {
    ssize_t n = ::send(socketFd_,
                       data.data() + written, data.size() - written,
                       MSG_DONTWAIT);
    if (n == -1) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      throwRuntime("failed to write to socket: {}", folly::errnoStr(errno));
    }
    written += n;
  }

  checkRuntime(written == data.size(),
               "failed to write to socket. Written {}, expected {}",
               written, data.size());
}

std::string ClientSocket::sendRequest(folly::StringPiece request,
                                      std::chrono::milliseconds timeout) {
  write(request, timeout);

  // wait for some data to arrive
  auto timeoutMs = static_cast<size_t>(timeout.count());
  for (size_t i = 0; i <= timeoutMs; ++i) {
    char replyBuf[kMaxReplySize + 1];
    ssize_t n = ::recv(socketFd_, replyBuf, kMaxReplySize, MSG_DONTWAIT);
    if (n == -1) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      throwRuntime("failed to read from socket: {}", folly::errnoStr(errno));
    } else if (n == 0) {
      throwRuntime("peer closed the socket");
    }
    return std::string(replyBuf, n);
  }
  throwRuntime("timeout reading from socket");
}

}}  // facebook::memcache
