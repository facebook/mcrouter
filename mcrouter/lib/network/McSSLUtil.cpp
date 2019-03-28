/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "McSSLUtil.h"

#include <folly/SharedMutex.h>
#include <folly/io/async/ssl/BasicTransportCertificate.h>
#include <folly/io/async/ssl/OpenSSLUtils.h>

#include "mcrouter/lib/network/SecurityOptions.h"

namespace facebook {
namespace memcache {

namespace {
static folly::SharedMutex& getMutex() {
  static folly::SharedMutex MUTEX;
  return MUTEX;
}

static McSSLUtil::SSLVerifyFunction& getAppFuncRef() {
  static McSSLUtil::SSLVerifyFunction VERIFIER;
  return VERIFIER;
}

static McSSLUtil::SSLFinalizeFunction& getServerFinalizeFuncRef() {
  static McSSLUtil::SSLFinalizeFunction FINALIZER;
  return FINALIZER;
}

static McSSLUtil::SSLFinalizeFunction& getClientFinalizeFuncRef() {
  static McSSLUtil::SSLFinalizeFunction FINALIZER;
  return FINALIZER;
}

class PlaintextWithCerts : public folly::AsyncSocket {
 public:
  using UniquePtr = std::
      unique_ptr<PlaintextWithCerts, folly::DelayedDestruction::Destructor>;
  using AsyncSocket::AsyncSocket;

  const X509* getSelfCert() const override {
    auto self = getSelfCertificate();
    if (self) {
      return self->getX509().get();
    }
    return nullptr;
  }

  folly::ssl::X509UniquePtr getPeerCert() const override {
    auto peer = getPeerCertificate();
    if (!peer) {
      return nullptr;
    }
    return peer->getX509();
  }

  std::string getSecurityProtocol() const override {
    return McSSLUtil::kTlsToPlainProtocolName;
  }
};
} // namespace

const std::string McSSLUtil::kTlsToPlainProtocolName = "mc_plaintext";

bool McSSLUtil::verifySSLWithDefaultBehavior(
    folly::AsyncSSLSocket*,
    bool preverifyOk,
    X509_STORE_CTX* ctx) noexcept {
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
  if (!folly::ssl::OpenSSLUtils::getPeerAddressFromX509StoreCtx(
          ctx, &addrStorage, &addrLen)) {
    return false;
  }
  return folly::ssl::OpenSSLUtils::validatePeerCertNames(
      cert, reinterpret_cast<sockaddr*>(&addrStorage), addrLen);
}

void McSSLUtil::setApplicationSSLVerifier(SSLVerifyFunction func) {
  folly::SharedMutex::WriteHolder wh(getMutex());
  getAppFuncRef() = std::move(func);
}

bool McSSLUtil::verifySSL(
    folly::AsyncSSLSocket* sock,
    bool preverifyOk,
    X509_STORE_CTX* ctx) noexcept {
  // It should be fine to hold onto the read holder since writes to this
  // will typically happen at app startup.
  folly::SharedMutex::ReadHolder rh(getMutex());
  auto& func = getAppFuncRef();
  if (!func) {
    return verifySSLWithDefaultBehavior(sock, preverifyOk, ctx);
  }
  return func(sock, preverifyOk, ctx);
}

void McSSLUtil::setApplicationServerSSLFinalizer(SSLFinalizeFunction func) {
  folly::SharedMutex::WriteHolder wh(getMutex());
  getServerFinalizeFuncRef() = std::move(func);
}

void McSSLUtil::setApplicationClientSSLFinalizer(SSLFinalizeFunction func) {
  folly::SharedMutex::WriteHolder wh(getMutex());
  getClientFinalizeFuncRef() = std::move(func);
}

void McSSLUtil::finalizeServerSSL(
    folly::AsyncTransportWrapper* transport) noexcept {
  folly::SharedMutex::ReadHolder rh(getMutex());
  auto& func = getServerFinalizeFuncRef();
  if (func) {
    func(transport);
  }
}

void McSSLUtil::finalizeClientSSL(
    folly::AsyncTransportWrapper* transport) noexcept {
  folly::SharedMutex::ReadHolder rh(getMutex());
  auto& func = getClientFinalizeFuncRef();
  if (func) {
    func(transport);
  }
}

bool McSSLUtil::negotiatedPlaintextFallback(
    const folly::AsyncSSLSocket& sock) noexcept {
  // get the negotiated protocol
  auto nextProto = sock.getApplicationProtocol();
  return nextProto == kMcSecurityTlsToPlaintextProto;
}

folly::AsyncTransportWrapper::UniquePtr McSSLUtil::moveToPlaintext(
    folly::AsyncSSLSocket& sock) noexcept {
  if (!negotiatedPlaintextFallback(sock)) {
    return nullptr;
  }

  // We need to mark the SSL as shutdown here, but need to do
  // it quietly so no alerts are sent over the wire.
  // This prevents SSL thinking we are shutting down in a bad state
  // when AsyncSSLSocket is cleaned up, which could remove the session
  // from the session cache
  auto ssl = const_cast<SSL*>(sock.getSSL());
  SSL_set_quiet_shutdown(ssl, 1);
  SSL_shutdown(ssl);

  // fallback to plaintext
  auto selfCert =
      folly::ssl::BasicTransportCertificate::create(sock.getSelfCertificate());
  auto peerCert =
      folly::ssl::BasicTransportCertificate::create(sock.getPeerCertificate());
  auto evb = sock.getEventBase();
  auto zcId = sock.getZeroCopyBufId();
  auto fd = sock.detachNetworkSocket();
  PlaintextWithCerts::UniquePtr res(new PlaintextWithCerts(evb, fd, zcId));
  res->setSelfCertificate(std::move(selfCert));
  res->setPeerCertificate(std::move(peerCert));
  return res;
}

} // namespace memcache
} // namespace facebook
