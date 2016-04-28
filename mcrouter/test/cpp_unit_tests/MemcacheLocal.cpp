/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MemcacheLocal.h"

#include <time.h>

#include <vector>

#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/Range.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/network/test/ClientSocket.h"

using namespace std;

namespace facebook { namespace memcache { namespace test {

// constants
constexpr int kPortSearchOut = 100;
constexpr int kPortRangeBegin = 30000;
constexpr int kPortRangeEnd = 50000;
constexpr int kTimeout = 10;
constexpr folly::StringPiece kConfig =
  "mcrouter/test/cpp_unit_tests/files/memcache_local_config.json";

MemcacheLocal::MemcacheLocal()
  : random_(time(nullptr)) {

  // find an open port and start memcache for our test.
  // blocks until server ready
  port_ = startMemcachedServer(kPortRangeBegin, kPortRangeEnd);
}

MemcacheLocal::~MemcacheLocal() {
  // stop the server. blocks until server exited
  stopMemcachedServer(port_);
}

bool MemcacheLocal::startMemcachedOnPort(std::string path, int port) {
  //open the output file
  int fd1 = open("/dev/null", O_RDWR);

  /**
   * options: redirect stdout and stderr of the subprocess to fd1 (/dev/null)
   * also send the subprocess a SIGTERM (terminate) signal when parent dies
   */
  folly::Subprocess::Options options;
  options.stdout(fd1).stderr(fd1).parentDeathSignal(SIGTERM);

  process_ = folly::make_unique<folly::Subprocess>(
      vector<string>{path, "-P", to_string(port)}, options);

  // premature exit?
  sleep(1);
  auto prc = process_->poll();
  if (prc.exited() || prc.killed()) {
    return false;
  }

  // busy wait and test if we can connect to the server
  for (int i = 0; i < kTimeout; i++) {
    try {
      ClientSocket socket(port);
      return true; // could start memcached server, and connect to it
    } catch (const std::runtime_error& e) { }
    sleep(1);
  }

  process_->kill(); // kill suprocess
  process_->wait(); // reap subprocess
  return false;
}

void MemcacheLocal::stopMemcachedServer(const int port) const {
  try {
    ClientSocket socket(port);
    socket.write("shutdown\r\n");
    process_->wait();
  } catch (const std::runtime_error& e) {
    process_->kill();
    process_->wait();
  }
}

int MemcacheLocal::generateRandomPort(const int startPort,
                                      const int stopPort) {
  int halfRange = (stopPort - startPort) >> 1;  // half the range
  std::uniform_int_distribution<int> dist(0, halfRange - 1);
  int halfPort = dist(random_);     // generate URN [0, half_range)
  return (halfPort << 1) + startPort; // ensures the generated port is even
}

int MemcacheLocal::startMemcachedServer(const int startPort,
                                        const int stopPort) {
  if (startPort > stopPort) {
    throw std::runtime_error("Port limit misconfiguration. Begin > End !");
  }

  /* find an open port */
  for (int i = 0; i < kPortSearchOut; i++) {
    int port = generateRandomPort(startPort, stopPort);

    // if memcached started successfully, return. Otherwise, keep begging.
    if (startMemcachedOnPort(mcrouter::getBinPath("mockmc"), port)) {
      return port;
    }
  }
  throw std::runtime_error("Can not find an open port to bind memcached");
}

std::string MemcacheLocal::generateMcrouterConfigString() {
  std::string config;
  if (!folly::readFile(kConfig.data(), config)) {
    throw std::runtime_error("Can not read " + kConfig.str());
  }
  config.replace(config.find("PORT"), 4, std::to_string(port_));
  return config;
}

}}} // namespace facebook::memcache::test
