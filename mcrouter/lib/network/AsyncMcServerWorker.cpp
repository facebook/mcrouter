/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsyncMcServerWorker.h"

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <folly/Memory.h>

#include "mcrouter/lib/network/ConnectionLRU.h"
#include "mcrouter/lib/network/McServerSession.h"

namespace facebook { namespace memcache {

AsyncMcServerWorker::AsyncMcServerWorker(AsyncMcServerWorkerOptions opts,
                                         folly::EventBase& eventBase)
    : opts_(std::move(opts)),
      eventBase_(eventBase) {
  if (opts_.connLRUopts.maxConns) {
    connLRU_.emplace(opts.connLRUopts);
  }
}

bool AsyncMcServerWorker::addSecureClientSocket(
    int fd,
    const std::shared_ptr<folly::SSLContext>& context,
    void* userCtxt) {
  folly::AsyncSSLSocket::UniquePtr sslSocket(
      new folly::AsyncSSLSocket(
          context, &eventBase_, fd, /* server = */ true));
  return addClientSocket(std::move(sslSocket), userCtxt);
}

bool AsyncMcServerWorker::addClientSocket(int fd, void* userCtxt) {
  auto socket = folly::AsyncSocket::UniquePtr(
      new folly::AsyncSocket(&eventBase_, fd));
  return addClientSocket(std::move(socket), userCtxt);
}

bool AsyncMcServerWorker::addClientSocket(
    folly::AsyncSocket::UniquePtr&& socket,
    void* userCtxt) {
  if (!onRequest_) {
    throw std::logic_error("can't add a socket without onRequest callback");
  }

  if (onAccepted_) {
    onAccepted_();
  }

  socket->setSendTimeout(opts_.sendTimeout.count());
  socket->setMaxReadsPerEvent(opts_.maxReadsPerEvent);
  socket->setNoDelay(true);
  int fd = socket->getFd();

  try {
    auto& session = McServerSession::create(
        std::move(socket),
        onRequest_,
        [this, fd](McServerSession& session) {
          if (connLRU_) {
            connLRU_->touchConnection(fd);
          }
          if (onWriteQuiescence_) {
            onWriteQuiescence_(session);
          }
        },
        onCloseStart_,
        [this, fd](McServerSession& session) {
          if (onCloseFinish_) {
            onCloseFinish_(session);
          }
          if (session.isLinked()) {
            sessions_.erase(sessions_.iterator_to(session));
          }
          if (connLRU_) {
            connLRU_->removeConnection(fd);
          }
        },
        onShutdown_,
        opts_,
        userCtxt,
        debugFifo_);

    sessions_.push_back(session);

    if (connLRU_ &&
        !connLRU_->addConnection(
            fd,
            std::unique_ptr<McServerSession, McServerSessionDeleter>(
                &session))) {
      // TODO: record stats about failure
    }
    return true;
  } catch (std::exception& ex) {
    return false;
  }
}

void AsyncMcServerWorker::shutdown() {
  if (!isAlive_) {
    return;
  }

  isAlive_ = false;
  /* Closing a session might cause it to remove itself from sessions_,
     so we should be careful with the iterator */
  auto it = sessions_.begin();
  while (it != sessions_.end()) {
    auto& session = *it;
    ++it;
    session.close();
  }
}

bool AsyncMcServerWorker::writesPending() const {
  for (auto& session : sessions_) {
    if (session.writesPending()) {
      return true;
    }
  }
  return false;
}

}}  // facebook::memcache
