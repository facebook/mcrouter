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

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace facebook { namespace memcache {

inline uint16_t getListenPort(int socketFd) {
  struct sockaddr_in sin;
  socklen_t len = sizeof(struct sockaddr_in);
  CHECK(!getsockname(socketFd, (struct sockaddr *)&sin, &len));
  return ntohs(sin.sin_port);
}

inline int createListenSocket() {
  struct addrinfo hints;
  struct addrinfo* res;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // Use all available interfaces, and choose an available port,
  // first try IPv6 address.
  if (getaddrinfo(nullptr, "0", &hints, &res)) {
    hints.ai_family = AF_INET;
    CHECK(!getaddrinfo(nullptr, "0", &hints, &res));
  }

  auto listen_socket =
    socket(res->ai_family, res->ai_socktype, res->ai_protocol);

  CHECK(listen_socket >= 0);
  CHECK(!bind(listen_socket, res->ai_addr, res->ai_addrlen));
  CHECK(!listen(listen_socket, SOMAXCONN));

  freeaddrinfo(res);

  return listen_socket;
}

}}  // facebook::memcache
