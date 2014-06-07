#include "AsyncMcServerWorker.h"

#include "folly/io/async/EventBase.h"
#include "folly/Memory.h"
#include "folly/MoveWrapper.h"
#include "mcrouter/lib/network/McServerSession.h"
#include "thrift/lib/cpp/async/TAsyncSocket.h"
#include "thrift/lib/cpp/async/TAsyncSSLSocket.h"
#include "thrift/lib/cpp/ssl/SSLUtils.h"

namespace facebook { namespace memcache {
namespace {
using apache::thrift::async::TAsyncSSLSocket;

class SimpleHandshakeCallback : public TAsyncSSLSocket::HandshakeCallback {
 public:
  bool handshakeVerify(TAsyncSSLSocket* sock,
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
    if (!apache::thrift::ssl::OpenSSLUtils::getPeerAddressFromX509StoreCtx(
            ctx, &addrStorage, &addrLen)) {
      return false;
    }
    return apache::thrift::ssl::OpenSSLUtils::validatePeerCertNames(
        cert, reinterpret_cast<sockaddr*>(&addrStorage), addrLen);
  }
  void handshakeSuccess(TAsyncSSLSocket *sock) noexcept override { }
  void handshakeError(
      TAsyncSSLSocket *sock,
      const apache::thrift::transport::TTransportException& ex)
    noexcept override { }
};
SimpleHandshakeCallback simpleHandshakeCallback;
} // anonymous

AsyncMcServerWorker::AsyncMcServerWorker(Options opts,
                                         folly::EventBase& eventBase)
    : opts_(std::move(opts)),
      eventBase_(eventBase) {
}

void AsyncMcServerWorker::addSecureClientSocket(
    int fd,
    const std::shared_ptr<apache::thrift::transport::SSLContext>& context) {
  apache::thrift::async::TAsyncSSLSocket::UniquePtr sslSocket(
      new apache::thrift::async::TAsyncSSLSocket(
          context, &eventBase_, fd, /* server = */ true));
  sslSocket->sslAccept(&simpleHandshakeCallback, /* timeout = */ 0);
  addClientSocket(std::move(sslSocket));
}

void AsyncMcServerWorker::addClientSocket(int fd) {
  auto socket = apache::thrift::async::TAsyncSocket::UniquePtr(
      new apache::thrift::async::TAsyncSocket(&eventBase_, fd));
  addClientSocket(std::move(socket));
}

void AsyncMcServerWorker::addClientSocket(
    apache::thrift::async::TAsyncSocket::UniquePtr&& socket) {
  if (!onRequest_) {
    throw std::logic_error("can't add a socket without onRequest callback");
  }

  if (onAccepted_) {
    onAccepted_();
  }

  socket->setSendTimeout(opts_.sendTimeout.count());
  socket->setMaxReadsPerEvent(opts_.maxReadsPerEvent);
  socket->setNoDelay(true);

  sessions_.insert(
    McServerSession::create(
      std::move(socket),
      onRequest_,
      [this] (std::shared_ptr<McServerSession> session) {
        /* We know that we keep alive all sessions until closed */
        assert(session != nullptr);

        if (onClosed_) {
          onClosed_();
        }
        sessions_.erase(session);
      },
      onShutdown_,
      opts_
    ));
}

void AsyncMcServerWorker::shutdown() {
  if (!isAlive_) {
    return;
  }

  isAlive_ = false;
  /* Closing a session might cause it to remove itself from sessions_,
     so save a copy first */
  auto sessions = sessions_;
  for (auto& session : sessions) {
    session->close();
  }
}

bool AsyncMcServerWorker::writesPending() const {
  for (auto& session : sessions_) {
    if (session->writesPending()) {
      return true;
    }
  }
  return false;
}

}}  // facebook::memcache
