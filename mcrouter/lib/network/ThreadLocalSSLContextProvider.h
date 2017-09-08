/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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

// only purpose here is to hold onto the session cache
class ClientSSLContext : public folly::SSLContext {
 public:
  ClientSSLContext() : folly::SSLContext() {}
  virtual ~ClientSSLContext() {
    detachFromCache();
  }
  void detachFromCache() {
    if (cache_) {
      wangle::SSLSessionCallbacks::detachCallbacksFromContext(
          getSSLCtx(), cache_.get());
      cache_ = nullptr;
    }
  }
  void attachToCache(
      const std::shared_ptr<wangle::SSLSessionCallbacks>& cache) {
    detachFromCache();
    if (cache) {
      cache_ = cache;
      wangle::SSLSessionCallbacks::attachCallbacksToContext(
          getSSLCtx(), cache_.get());
    }
  }
  std::shared_ptr<wangle::SSLSessionCallbacks> getCache() {
    return cache_;
  }

 private:
  std::shared_ptr<wangle::SSLSessionCallbacks> cache_;
};

/**
 * Manages sets of certificates on per thread basis.
 * Each set will be loaded only once per thread and will be reloaded if it's
 * older than 5 minutes.
 */
std::shared_ptr<folly::SSLContext> getSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    folly::Optional<wangle::TLSTicketKeySeeds> = folly::none,
    bool clientContext = false);

} // memcache
} // facebook
