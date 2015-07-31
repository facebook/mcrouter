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

#include <string>
#include <chrono>

#include "mcrouter/lib/network/ConnectionLRUOptions.h"

namespace facebook { namespace memcache {

struct AsyncMcServerWorkerOptions {
  /**
   * When set AsyncMcServer returns the default version string. If not,
   * the server is responsible handling the version commands.
   */
  bool defaultVersionHandler{true};
  /**
   * String that will be returned for 'VERSION' commands.
   */
  std::string versionString{"AsyncMcServer-1.0"};

  /**
   * Timeout for writes (i.e. replies to the clients).
   * If 0, no timeout.
   */
  std::chrono::milliseconds sendTimeout{0};

  /**
   * Maximum number of unreplied requests allowed before
   * we stop reading from client sockets.
   * If 0, there is no limit.
   */
  size_t maxInFlight{0};

  /**
   * Maximum number of read system calls per event loop iteration.
   * If 0, there is no limit.
   *
   * If a socket has available data to read, we'll keep calling read()
   * on it this many times before we do any writes.
   *
   * For heavy workloads, larger values may hurt latency
   * but increase throughput.
   */
  uint16_t maxReadsPerEvent{0};

  /**
   * If non-zero, the buffer size will be dynamically adjusted
   * to contain roughly this many requests, within min/max limits below.
   *
   * The intention is to limit the number of requests processed
   * per loop iteration. Smaller values may improve latency.
   *
   * If 0, buffer size is always maxBufferSize.
   */
  size_t requestsPerRead{0};

  /**
   * Options pertaining to the LRU of connections.
   */
  ConnectionLRUOptions connLRUopts;

  /**
   * Smallest allowed buffer size.
   */
  size_t minBufferSize{256};

  /**
   * Largest allowed buffer size.
   */
  size_t maxBufferSize{4096};

  /**
   * If true, we attempt to write every reply to the socket
   * immediately.  If the write cannot be fully completed (i.e. not
   * enough TCP memory), all reading is paused until after the write
   * is completed.
   */
  bool singleWrite{false};
};

}}  // facebook::memcache
