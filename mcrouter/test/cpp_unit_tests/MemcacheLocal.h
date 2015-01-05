/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef MEMCACHE_LOCAL_H
#define MEMCACHE_LOCAL_H 1

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>

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
   * Constructor: If no argument is passed, then it finds an appropriate port
   * and starts the local memcached server. If a port has been passed then
   * it tries to start the server on the specified  port.
   * Object construction blocks until the memcache server is ready.
   * fatal error if can not start new process
   */
  explicit MemcacheLocal(const int port = 0);

  /* which port memcache server is listening? */
  int getPort() const { return port_; }

  /* mcrouter needs to be configured to use the locally started memcached */
  std::string generateMcrouterConfigString();

  /**
   * Destructor: Stops the memcached server
   */
  ~MemcacheLocal();

  // constants
  // hostname for the memcached server, memcached path, memcached binary
  const static std::string kHostName, kMemcachedPath, kMemcachedBin;
  // fail test if we searched PORT_SEARCH_TIMES for an open port
  const static int kPortSearchOut;
  // after starting local memcached, try to connect at most kTimeout times
  const static int kTimeout;
  // range of search for an open port
  const static int kPortRangeBegin, kPortRangeEnd;

 private:
  /* process of the started memcache */
  std::unique_ptr<folly::Subprocess> process_;
  std::mt19937 random_;
  int port_; /* port that was used to bind the server */

  /**
   * Start memcached on a local port
   */
  int startMemcachedServer(const int start_port, const int stop_port);

  /* translate a URN[0,n] to URN[a,a+n] */
  int generateRandomPort(const int start_port, const int stop_port);

  /* get sockaddr, IPv4 or IPv6 */
  void* getSocketAddress(struct sockaddr *sa) const;

  /**
   * connects to a tcp server using hostname and port as input
   * can be used for actual communication, or
   * just to test if anything is running
   * if successfully connected, returns a socket,
   * if can not connect, returns -1
   */
  int connectTcpServer(const std::string& hostname, const int port) const;

  /* send a message through an established socket */
  int sendMessage(const int sockfd, const std::string& message) const;

  /* start a memcache server on the specified port */
  int startMemcachedOnPort(const std::string& path,
                           const std::string& bin, const int port);

  /* stop memcache server */
  void stopMemcachedServer(const int port) const;
};

} } } // !namespace facebook::memcache::test

#endif
