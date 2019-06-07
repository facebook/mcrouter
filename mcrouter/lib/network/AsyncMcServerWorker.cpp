/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "AsyncMcServerWorker.h"

#include <memory>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>

#include "mcrouter/lib/network/McFizzServer.h"
#include "mcrouter/lib/network/McServerSession.h"

namespace facebook {
namespace memcache {

AsyncMcServerWorker::AsyncMcServerWorker(
    AsyncMcServerWorkerOptions opts,
    folly::EventBase& eventBase)
    : opts_(std::move(opts)), eventBase_(eventBase), tracker_(opts_.maxConns) {}

bool AsyncMcServerWorker::addSecureClientSocket(
    int fd,
    AsyncMcServerWorker::ContextPair contexts,
    void* userCtxt) {
  McFizzServer::UniquePtr socket(new McFizzServer(
      folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(
          &eventBase_, folly::NetworkSocket::fromFd(fd))),
      std::move(contexts.second),
      std::move(contexts.first)));
  return addClientSocket(std::move(socket), userCtxt);
}

bool AsyncMcServerWorker::addClientSocket(int fd, void* userCtxt) {
  auto socket = folly::AsyncSocket::UniquePtr(
      new folly::AsyncSocket(&eventBase_, folly::NetworkSocket::fromFd(fd)));
  return addClientSocket(std::move(socket), userCtxt);
}

bool AsyncMcServerWorker::addClientSocket(
    folly::AsyncTransportWrapper::UniquePtr transport,
    void* userCtxt) {
  auto socket = transport->getUnderlyingTransport<folly::AsyncSocket>();
  CHECK(socket) << "Underlying transport expected to be AsyncSocket";
  McServerSession::applySocketOptions(*socket, opts_);
  return addClientTransport(std::move(transport), userCtxt);
}

McServerSession* AsyncMcServerWorker::addClientTransport(
    folly::AsyncTransportWrapper::UniquePtr transport,
    void* userCtxt) {
  if (!onRequest_) {
    throw std::logic_error("can't add a transport without onRequest callback");
  }

  try {
    return std::addressof(tracker_.add(
        std::move(transport),
        onRequest_,
        opts_,
        userCtxt,
        compressionCodecMap_));
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error creating new session: " << ex.what();
    return nullptr;
  }
}

void AsyncMcServerWorker::shutdown() {
  if (!isAlive_) {
    return;
  }

  isAlive_ = false;
  tracker_.closeAll();
}

bool AsyncMcServerWorker::writesPending() const {
  return tracker_.writesPending();
}

} // namespace memcache
} // namespace facebook
