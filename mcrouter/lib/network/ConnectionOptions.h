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

#include <thrift/lib/cpp/async/TAsyncSocket.h>

#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/AccessPoint.h"

namespace folly {
class SSLContext;
} // folly

namespace facebook { namespace memcache {

/**
 * A struct for storing all connection related options.
 */
struct ConnectionOptions {
  typedef apache::thrift::async::TAsyncSocket::OptionMap SocketOptions;

  ConnectionOptions(folly::StringPiece host_,
                    uint16_t port_,
                    mc_protocol_t protocol_)
    : accessPoint(host_.str(), port_, protocol_) {
  }

  explicit ConnectionOptions(AccessPoint ap) : accessPoint(std::move(ap)) {
  }

  /**
   * For performance testing only.
   * If this flag is set, each request won't be sent over network, instead it
   * will be processed by fake transport, that will reply each request with some
   * basic reply (e.g. STORED, DELETED, or some random string for get requests).
   * Currently has no effect if protocol is mc_umbrella_protocol.
   */
  bool noNetwork{false};

  /**
   * Access point of the destination.
   */
  AccessPoint accessPoint;

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
   * Informs whether QoS is enabled.
   */
  bool enableQoS{false};

  /*
   * QoS class to use in packages.
   */
  unsigned int qos{0};

  /**
   * SSLContext provider callback. If null, then unsecured connections will be
   * established, else it will be called for each attempt to establish
   * connection.
   */
  std::function<std::shared_ptr<folly::SSLContext>()>
    sslContextProvider;
};

}} // facebook::memcache
