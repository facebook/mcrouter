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

#include <chrono>
#include <memory>

#include <folly/io/async/AsyncSocket.h>

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
  typedef folly::AsyncSocket::OptionMap SocketOptions;

  ConnectionOptions(folly::StringPiece host_,
                    uint16_t port_,
                    mc_protocol_t protocol_)
    : accessPoint(std::make_shared<AccessPoint>(host_, port_, protocol_)) {
  }

  explicit ConnectionOptions(std::shared_ptr<const AccessPoint> ap)
    : accessPoint(std::move(ap)) {
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
   * Flag to enable serialization in the Caret format for the umbrellaProtocol.
   */
  bool useTyped{false};

  /**
   * Access point of the destination.
   */
  std::shared_ptr<const AccessPoint> accessPoint;

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
   * Send timeout in ms. Shoud be used only for async (non-fiber) mode.
   */
  std::chrono::milliseconds sendTimeout{0};

  /**
   * Write/connect timeout in ms.
   */
  std::chrono::milliseconds writeTimeout{0};

  /**
   * Informs whether QoS is enabled.
   */
  bool enableQoS{false};

  /*
   * QoS class to use in packages.
   */
  unsigned int qosClass{0};

  /*
   * QoS path to use in packages.
   */
  unsigned int qosPath{0};

  /**
   * SSLContext provider callback. If null, then unsecured connections will be
   * established, else it will be called for each attempt to establish
   * connection.
   */
  std::function<std::shared_ptr<folly::SSLContext>()>
    sslContextProvider;

  /**
   * Path of the debug fifo.
     * If empty, debug fifo is disabled.
   */
  std::string debugFifoPath;
};

}} // facebook::memcache
