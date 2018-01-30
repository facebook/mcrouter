/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsyncMcServerWorker.h"

#include <memory>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>

#include "mcrouter/lib/network/McServerSession.h"

namespace facebook {
namespace memcache {

AsyncMcServerWorker::AsyncMcServerWorker(
    AsyncMcServerWorkerOptions opts,
    folly::EventBase& eventBase)
    : opts_(std::move(opts)), eventBase_(eventBase), tracker_(opts_.maxConns) {}

bool AsyncMcServerWorker::addSecureClientSocket(
    int fd,
    const std::shared_ptr<folly::SSLContext>& context,
    void* userCtxt) {
  folly::AsyncSSLSocket::UniquePtr sslSocket(
      new folly::AsyncSSLSocket(context, &eventBase_, fd, /* server = */ true));
  return addClientSocket(std::move(sslSocket), userCtxt);
}

bool AsyncMcServerWorker::addClientSocket(int fd, void* userCtxt) {
  auto socket =
      folly::AsyncSocket::UniquePtr(new folly::AsyncSocket(&eventBase_, fd));
  return addClientSocket(std::move(socket), userCtxt);
}

bool AsyncMcServerWorker::addClientSocket(
    folly::AsyncSocket::UniquePtr socket,
    void* userCtxt) {
  socket->setMaxReadsPerEvent(opts_.maxReadsPerEvent);
  socket->setNoDelay(true);
  return addClientTransport(std::move(socket), userCtxt);
}

McServerSession* AsyncMcServerWorker::addClientTransport(
    folly::AsyncTransportWrapper::UniquePtr transport,
    void* userCtxt) {
  if (!onRequest_) {
    throw std::logic_error("can't add a transport without onRequest callback");
  }

  if (onAccepted_) {
    onAccepted_();
  }

  transport->setSendTimeout(opts_.sendTimeout.count());

  try {
    return std::addressof(tracker_.add(
        std::move(transport),
        onRequest_,
        opts_,
        userCtxt,
        compressionCodecMap_));
  } catch (const std::exception& ex) {
    // TODO: record stats about failure
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

} // memcache
} // facebook
