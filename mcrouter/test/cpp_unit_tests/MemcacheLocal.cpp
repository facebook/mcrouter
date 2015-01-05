/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MemcacheLocal.h"

#include <vector>

#include <folly/FileUtil.h>
#include <folly/Memory.h>

#include "mcrouter/config.h"

using namespace folly;
using namespace std;

namespace facebook { namespace memcache { namespace test {

// constants
const std::string MemcacheLocal::kHostName = "localhost";
const int MemcacheLocal::kPortSearchOut = 100;
const int MemcacheLocal::kPortRangeBegin = 30000;
const int MemcacheLocal::kPortRangeEnd = 50000;
const int MemcacheLocal::kTimeout = 10;
const std::string MemcacheLocal::kMemcachedPath
  = MCROUTER_INSTALL_PATH "mcrouter/lib/network/mock_mc_server";
const std::string MemcacheLocal::kMemcachedBin = "mock_mc_server";
const std::string kConfig =
  "mcrouter/test/cpp_unit_tests/files/memcache_local_config.json";

MemcacheLocal::MemcacheLocal(const int port)
: random_(time(nullptr)) { //initialize random
  process_ = nullptr;

  if (port == 0) {
    // find an open port and start memcache for our test.
    // blocks until server ready
    port_ = startMemcachedServer(kPortRangeBegin, kPortRangeEnd);
  } else {
    port_ = port;
    if (!startMemcachedOnPort(kMemcachedPath, kMemcachedBin, port_)) {
      throw std::runtime_error("Failed to start memcached on port "
                               + to_string(port_));
    }
  }
}

MemcacheLocal::~MemcacheLocal() {
  // stop the server. blocks until server exited
  stopMemcachedServer(port_);
}

int MemcacheLocal::startMemcachedOnPort(const string& path,
                                        const string& bin, const int port) {
  //memcache prints some lines. redirect output to file / silence output.
  int fd1;
  //open the output file
  fd1 = open("/dev/null", O_RDWR);

  /**
   * options: redirect stdout and stderr of the subprocess to fd1 (/dev/null)
   * also send the subprocess a SIGTERM (terminate) signal when parent dies
   */
  Subprocess::Options options;
  options.stdout(fd1).stderr(fd1).parentDeathSignal(SIGTERM);

  process_ = folly::make_unique<Subprocess>(
                                            vector<string> {
                                              bin, "-P", to_string(port)
                                            }, options, path.c_str(), nullptr);

  int i, socket_descriptor;

  // premature exit?
  sleep(1);
  ProcessReturnCode prc = process_->poll();
  if (prc.exited() || prc.killed()) {
    return 0;
  }

  // busy wait and test if we can connect to the server
  for (i = 0; i < kTimeout; i++) { // try kTimeOut times to connect
    socket_descriptor = connectTcpServer(kHostName, port);
    if (socket_descriptor != -1) {
      break;
    }
    sleep(1);
  }

  if (i == kTimeout) { // could not start memcached server
    process_->kill(); // kill suprocess
    process_->wait(); // reap subprocess
    return 0;
  }
  // otherwise, socket_descriptor is a valid, connected socket
  // close the socket
  close(socket_descriptor);
  return 1; // could start memcached server, and connect to it
}

void MemcacheLocal::stopMemcachedServer(const int port) const {
  int socket_descriptor;

  socket_descriptor = connectTcpServer(kHostName, port);
  if (socket_descriptor != -1) {
    // Mr. Memcached server, please shutdown
    sendMessage(socket_descriptor, "shutdown\r\n");
    close(socket_descriptor); // close the socket
    process_->wait();
    // yes, he is finished
  } else {
    // how come we can't connect to server?
    throw std::runtime_error("No transport to memcached server");
  }
}

// assumes an established socket
int MemcacheLocal::sendMessage(const int sockfd, const string& message) const {
  int nchar;
  const char *msg = message.c_str();
  nchar = send(sockfd, msg, strlen(msg), 0);
  if (nchar == -1) {
    throw std::runtime_error("Can not send command through socket");
  }
  return nchar;
}

void * MemcacheLocal::getSocketAddress(struct sockaddr *sa) const {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

int MemcacheLocal::connectTcpServer(const string& hostname,
                                    const int port) const {
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];

  string port_string = to_string(port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(hostname.c_str(),
                        port_string.c_str(),
                        &hints, &servinfo)) != 0) {
    // we can not get address info. network problem
    throw std::runtime_error("getaddrinfo fails");
  }

  // loop through all the results and connect to the first we can
  for (p = servinfo; p != nullptr; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      continue;
    }
    break;
  }

  if (p == nullptr) {
    return -1; // failed to connect
  }

  inet_ntop(p->ai_family,
            getSocketAddress((struct sockaddr *)p->ai_addr),
            s, sizeof s);
  // connecting
  freeaddrinfo(servinfo); // all done with this structure
  return sockfd;
}

int MemcacheLocal::generateRandomPort(const int start_port,
                                      const int stop_port) {
  int half_range = (stop_port - start_port) >> 1;  // half the range
  std::uniform_int_distribution<int> dist(0, half_range - 1);
  int half_port = dist(random_);     // generate URN [0, half_range)
  return (half_port << 1) + start_port; // ensures the generated port is even
}

int MemcacheLocal::startMemcachedServer(const int start_port,
                                        const int stop_port) {
  /* find an open port */
  int port;
  int i;

  if (start_port > stop_port)
    throw std::runtime_error("Port limit misconfiguration. Begin > End !");

  for (i = 0; i < kPortSearchOut; i++) {
    port = generateRandomPort(start_port, stop_port);

    // if memcached started successfully, return. Otherwise, keep begging.
    if (startMemcachedOnPort(kMemcachedPath, kMemcachedBin, port)) {
      break;
    }
  }
  if (i == kPortSearchOut) { // so many futile search for an open port!!
    // something must be wrong
    throw std::runtime_error("Can not find an open port to bind memcached");
  }
  return port;
}

std::string MemcacheLocal::generateMcrouterConfigString() {
  std::string config;
  if (!folly::readFile(kConfig.data(), config)) {
    throw std::runtime_error("Can not read " + kConfig);
  }
  config.replace(config.find("PORT"), 4, std::to_string(port_));
  return config;
}

} } } // !namespace facebook::memcache::test
