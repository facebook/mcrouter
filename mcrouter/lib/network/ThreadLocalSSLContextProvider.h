/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <memory>

#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/io/async/SSLContext.h>
#include <wangle/client/ssl/SSLSessionCallbacks.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

namespace folly {
class SSLContext;
} // folly

namespace facebook {
namespace memcache {

class SSLContextConfig;

class ClientSSLContext : public folly::SSLContext {
 public:
  explicit ClientSSLContext(wangle::SSLSessionCallbacks& cache)
      : cache_(cache) {
    wangle::SSLSessionCallbacks::attachCallbacksToContext(getSSLCtx(), &cache_);
  }

  virtual ~ClientSSLContext() override {
    wangle::SSLSessionCallbacks::detachCallbacksFromContext(
        getSSLCtx(), &cache_);
  }

  wangle::SSLSessionCallbacks& getCache() {
    return cache_;
  }

 private:
  // In our usage, cache_ is a LeakySingleton so the raw reference is safe.
  wangle::SSLSessionCallbacks& cache_;
};

/**
 * The following methods return thread local managed SSL Contexts.  Contexts are
 * reloaded on demand if they are 30 minutes old on a per thread basis.
 */

/**
 * Get a context used for client connections.  If pemCaPath is not empty, the
 * context will be configured to verify server ceritifcates against the CA.
 * pemCertPath and pemKeyPath may be empty.
 */
std::shared_ptr<folly::SSLContext> getClientContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath);

/**
 * Get a context used for accepting ssl connections.  All paths must not be
 * empty.
 * If requireClientCerts is true, clients that do not present a client cert
 * during the handshake will be rejected.
 */
std::shared_ptr<folly::SSLContext> getServerContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    bool requireClientCerts,
    folly::Optional<wangle::TLSTicketKeySeeds> seeds);

} // memcache
} // facebook
