/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McServerSSLUtil.h"

#include <folly/SharedMutex.h>
#include <folly/io/async/ssl/OpenSSLUtils.h>

namespace facebook {
namespace memcache {

namespace {
static folly::SharedMutex& getMutex() {
  static folly::SharedMutex MUTEX;
  return MUTEX;
}

static McServerSSLUtil::SSLVerifyFunction& getAppFuncRef() {
  static McServerSSLUtil::SSLVerifyFunction VERIFIER;
  return VERIFIER;
}
} // namespace

bool McServerSSLUtil::verifySSLWithDefaultBehavior(
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

void McServerSSLUtil::setApplicationSSLVerifier(SSLVerifyFunction func) {
  folly::SharedMutex::WriteHolder wh(getMutex());
  getAppFuncRef() = std::move(func);
}

bool McServerSSLUtil::verifySSL(
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

} // namespace memcache
} // namespace facebook
