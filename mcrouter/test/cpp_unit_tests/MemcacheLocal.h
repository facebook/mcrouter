/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <random>
#include <string>

#include <folly/Subprocess.h>

/**
 * This class is used to create start a memcache server
 * on the local machine, intended for test reasons.
 * It finds an appropriate port to start the memcache server,
 * starts the server on that port. It also handles cleanup when
 * the parent process exits.
 */
namespace facebook { namespace memcache { namespace test {

class MemcacheLocal {
 public:
  /**
   * finds an appropriate port and starts the local memcached server.
   * Object construction blocks until the memcache server is ready.
   * @throws runtime_error if can not start new process
   */
  MemcacheLocal();

  /* which port memcache server is listening? */
  int getPort() const { return port_; }

  /* mcrouter needs to be configured to use the locally started memcached */
  std::string generateMcrouterConfigString();

  /**
   * Destructor: Stops the memcached server
   */
  ~MemcacheLocal();

 private:
  /* process of the started memcache */
  std::unique_ptr<folly::Subprocess> process_;
  std::mt19937 random_;
  int port_{-1}; /* port that was used to bind the server */

  /**
   * Start memcached on a local port
   */
  int startMemcachedServer(const int startPort, const int stopPort);

  /* translate a URN[0,n] to URN[a,a+n] */
  int generateRandomPort(const int startPort, const int stopPort);

  /* start a memcache server on the specified port */
  bool startMemcachedOnPort(std::string path, int port);

  /* stop memcache server */
  void stopMemcachedServer(const int port) const;
};

}}} // !namespace facebook::memcache::test
