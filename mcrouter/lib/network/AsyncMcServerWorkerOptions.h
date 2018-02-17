/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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
#include <string>

namespace facebook {
namespace memcache {

class CpuController;
class MemoryController;

struct AsyncMcServerWorkerOptions {
  /**
   * When set AsyncMcServer returns the default version string. If not,
   * the server is responsible handling the version commands.
   */
  bool defaultVersionHandler{true};

  /**
   * If true, we attempt to write every reply to the socket
   * immediately.  If the write cannot be fully completed (i.e. not
   * enough TCP memory), all reading is paused until after the write
   * is completed.
   */
  bool singleWrite{false};

  /**
   * If true, time measurement in event base is enabled.
   */
  bool enableEventBaseTimeMeasurement{false};

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
   * Max connections used at any moment.
   */
  size_t maxConns{0};

  /**
   * Smallest allowed buffer size.
   */
  size_t minBufferSize{256};

  /**
   * Largest allowed buffer size.
   */
  size_t maxBufferSize{4096};

  /**
   * String that will be returned for 'VERSION' commands.
   */
  std::string versionString{"AsyncMcServer-1.0"};

  /**
   * Path of the debug fifo.
   * If empty, debug fifo is disabled.
   */
  std::string debugFifoPath;

  /**
   * The congestion controller for CPU utilization at the server.
   */
  std::shared_ptr<CpuController> cpuController;

  /**
   * The congestion controller for memory utilization at the server.
   */
  std::shared_ptr<MemoryController> memController;

  /**
   * EXPERIMENTAL FEATURE!
   *
   * If non-zero, enables server sending OOB GoAway messages to clients,
   * signaling them that the server is about to disappear and the client should
   * stop sending requests over this connection after processing this message.
   */
  std::chrono::milliseconds goAwayTimeout{0};
};
} // memcache
} // facebook
