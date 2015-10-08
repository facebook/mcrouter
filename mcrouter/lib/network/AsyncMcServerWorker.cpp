/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsyncMcServerWorker.h"

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/SSLContext.h>

#include "mcrouter/lib/network/ConnectionLRU.h"
#include "mcrouter/lib/network/McServerSession.h"

namespace facebook { namespace memcache {
namespace {
using folly::AsyncSSLSocket;

class SimpleHandshakeCallback : public AsyncSSLSocket::HandshakeCB {
 public:
  bool handshakeVer(AsyncSSLSocket* sock,
                    bool preverifyOk,
                    X509_STORE_CTX* ctx) noexcept override {
    if (!preverifyOk) {
      return false;
    }
    // XXX I'm assuming that this will be the case as a result of
    // preverifyOk being true
    DCHECK(X509_STORE_CTX_get_error(ctx) == X509_V_OK);

    // So the interesting thing is that this always returns the depth of
    // the cert it's asking you to verify, and the error_ assumes to be
    // just a poorly named function.
    auto certDepth = X509_STORE_CTX_get_error_depth(ctx);

    // Depth is numbered from the peer cert going up.  For anything in the
    // chain, let's just leave it to openssl to figure out it's validity.
    // We may want to limit the chain depth later though.
    if (certDepth != 0) {
      return preverifyOk;
    }

    auto cert = X509_STORE_CTX_get_current_cert(ctx);
    sockaddr_storage addrStorage;
    socklen_t addrLen = 0;
    if (!folly::OpenSSLUtils::getPeerAddressFromX509StoreCtx(
            ctx, &addrStorage, &addrLen)) {
      return false;
    }
    return folly::OpenSSLUtils::validatePeerCertNames(
        cert, reinterpret_cast<sockaddr*>(&addrStorage), addrLen);
  }
  void handshakeSuc(AsyncSSLSocket *sock) noexcept override { }
  void handshakeErr(
      AsyncSSLSocket *sock,
      const folly::AsyncSocketException& ex)
    noexcept override { }
};
SimpleHandshakeCallback simpleHandshakeCallback;
} // anonymous

AsyncMcServerWorker::AsyncMcServerWorker(AsyncMcServerWorkerOptions opts,
                                         folly::EventBase& eventBase)
    : opts_(std::move(opts)),
      eventBase_(eventBase) {
  if (opts_.connLRUopts.maxConns) {
    connLRU_.emplace(opts.connLRUopts);
  }
}

void AsyncMcServerWorker::addSecureClientSocket(
    int fd,
    const std::shared_ptr<folly::SSLContext>& context,
    void* userCtxt) {
  folly::AsyncSSLSocket::UniquePtr sslSocket(
      new folly::AsyncSSLSocket(
          context, &eventBase_, fd, /* server = */ true));
  sslSocket->sslAccept(&simpleHandshakeCallback, /* timeout = */ 0);
  addClientSocket(std::move(sslSocket), userCtxt);
}

void AsyncMcServerWorker::addClientSocket(int fd, void* userCtxt) {
  auto socket = folly::AsyncSocket::UniquePtr(
      new folly::AsyncSocket(&eventBase_, fd));
  addClientSocket(std::move(socket), userCtxt);
}

void AsyncMcServerWorker::addClientSocket(
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

  auto& session = McServerSession::create(
    std::move(socket),
    onRequest_,
    [this, fd] (McServerSession& session) {
      if (connLRU_) {
        connLRU_->touchConnection(fd);
      }
      if (onWriteQuiescence_) {
        onWriteQuiescence_(session);
      }
    },
    onCloseStart_,
    [this, fd] (McServerSession& session) {
      if (onCloseFinish_) {
        onCloseFinish_(session);
      }
      sessions_.erase(sessions_.iterator_to(session));
      if (connLRU_) {
        connLRU_->removeConnection(fd);
      }
    },
    onShutdown_,
    opts_,
    userCtxt,
    debugFifo_
  );

  sessions_.push_back(session);

  if (connLRU_ &&
      !connLRU_->addConnection(
          fd,
          std::unique_ptr<McServerSession, McServerSessionDeleter>(&session))) {
    // TODO: record stats about failure
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
