/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <chrono>

#include "mcrouter/lib/mc/protocol.h"
#include "thrift/lib/cpp/async/TAsyncSocket.h"

namespace apache { namespace thrift { namespace transport {
class SSLContext;
}}} // apache::thrift::transport

namespace facebook { namespace memcache {

/**
 * A struct for storing all connection related options.
 */
struct ConnectionOptions {
  typedef apache::thrift::async::TAsyncSocket::OptionMap SocketOptions;

  ConnectionOptions(folly::StringPiece host_,
                    uint16_t port_,
                    mc_protocol_t protocol_) :
      host(host_.begin(), host_.end()),
      port(port_),
      protocol(protocol_) {
  }

  /**
   * Hostname of the peer to connect to.
   */
  std::string host;

  /**
   * Port of the peer to connect to.
   */
  uint16_t port{0};

  /**
   * Type of protocol that should be used.
   */
  mc_protocol_t protocol{mc_unknown_protocol};

  /**
   * Number of TCP KeepAlive probes to send before considering connection dead.
   *
   * Note: Option will be enabled iff it is supported by the OS and this
   *       value > 0.
   */
  int tcpKeepAliveCount{0};

  /**
   * Interval between last data packet sent and the first TCP KeepAlive probe.
   */
  int tcpKeepAliveIdle{0};

  /**
   * Interval between two consequent TCP KeepAlive probes.
   */
  int tcpKeepAliveInterval{0};

  /**
   * Send/connect timeout in ms.
   */
  std::chrono::milliseconds timeout{0};

  /**
   * SSLContext provider callback. If null, then unsecured connections will be
   * established, else it will be called for each attempt to establish
   * connection.
   */
  std::function<std::shared_ptr<apache::thrift::transport::SSLContext>()>
    sslContextProvider;
};

}} // facebook::memcache
