/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ThreadLocalSSLContextProvider.h"

#include <unordered_map>

#include <folly/Singleton.h>
#include <folly/hash/Hash.h>
#include <folly/io/async/SSLContext.h>
#include <wangle/client/persistence/SharedMutexCacheLockGuard.h>
#include <wangle/client/ssl/SSLSessionCacheData.h>
#include <wangle/client/ssl/SSLSessionPersistentCache.h>
#include <wangle/ssl/SSLCacheOptions.h>
#include <wangle/ssl/SSLContextConfig.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <wangle/ssl/ServerSSLContext.h>
#include <wangle/ssl/TLSTicketKeyManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

using folly::SSLContext;

namespace facebook {
namespace memcache {

namespace {

/* Sessions are valid for upto 24 hours */
constexpr size_t kSessionLifeTime = 86400;

struct CertPaths {
  folly::StringPiece pemCertPath;
  folly::StringPiece pemKeyPath;
  folly::StringPiece pemCaPath;
  bool isClient;

  bool operator==(const CertPaths& other) const {
    return pemCertPath == other.pemCertPath && pemKeyPath == other.pemKeyPath &&
        pemCaPath == other.pemCaPath && isClient == other.isClient;
  }
};

struct ContextInfo {
  std::string pemCertPath;
  std::string pemKeyPath;
  std::string pemCaPath;

  std::shared_ptr<SSLContext> context;

  std::chrono::time_point<std::chrono::steady_clock> lastLoadTime;
};

struct CertPathsHasher {
  size_t operator()(const CertPaths& paths) const {
    return folly::Hash()(
        paths.pemCertPath, paths.pemKeyPath, paths.pemCaPath, paths.isClient);
  }
};

void logCertFailure(
    folly::StringPiece name,
    folly::StringPiece path,
    const std::exception& ex) {
  LOG_FAILURE(
      "SSLCert",
      failure::Category::kBadEnvironment,
      "Failed to load {} from \"{}\", ex: {}",
      name,
      path,
      ex.what());
}

bool configureSSLContext(
    folly::SSLContext& sslContext,
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath) {
  // Load certificate.
  try {
    sslContext.loadCertificate(pemCertPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("certificate", pemCertPath, ex);
    return false;
  }

  // Load private key.
  try {
    sslContext.loadPrivateKey(pemKeyPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("private key", pemKeyPath, ex);
    return false;
  }

  // Load trusted certificates.
  try {
    sslContext.loadTrustedCertificates(pemCaPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("trusted certificates", pemCaPath, ex);
    return false;
  }

  // Load client CA list.
  try {
    sslContext.loadClientCAList(pemCaPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("client CA list", pemCaPath, ex);
    return false;
  }

// Try to disable compression if possible to reduce CPU and memory usage.
#ifdef SSL_OP_NO_COMPRESSION
  try {
    sslContext.setOptions(SSL_OP_NO_COMPRESSION);
  } catch (const std::runtime_error& ex) {
    LOG_FAILURE(
        "SSLCert",
        failure::Category::kSystemError,
        "Failed to apply SSL_OP_NO_COMPRESSION flag onto SSLContext "
        "with files: pemCertPath='{}', pemKeyPath='{}', pemCaPath='{}'",
        pemCertPath,
        pemKeyPath,
        pemCaPath);
    // We failed to disable compression, but the SSLContext itself is good to
    // use.
  }
#endif
  return true;
}

using TicketCacheLayer = wangle::LRUPersistentCache<
    std::string,
    wangle::SSLSessionCacheData,
    folly::SharedMutex>;

// a SSL session cache that keys off string
class SSLTicketCache
    : public wangle::SSLSessionPersistentCacheBase<std::string> {
 public:
  explicit SSLTicketCache(std::shared_ptr<TicketCacheLayer> cache)
      : wangle::SSLSessionPersistentCacheBase<std::string>(std::move(cache)) {}

 protected:
  std::string getKey(const std::string& identity) const override {
    return identity;
  }
};

// global thread safe ticket cache
// TODO(jmswen) Try to come up with a cleaner approach here that doesn't require
// leaking.
folly::LeakySingleton<SSLTicketCache> ticketCache([] {
  // create cache layer of max size 100;
  auto cacheLayer = std::make_shared<TicketCacheLayer>(100);
  return new SSLTicketCache(std::move(cacheLayer));
});

std::shared_ptr<SSLContext> createServerSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    folly::Optional<wangle::TLSTicketKeySeeds> ticketKeySeeds) {
  wangle::SSLContextConfig cfg;
  // don't need to set any certs on the cfg since the context is configured
  // in configureSSLContext;
  cfg.sessionTicketEnabled = true;
  cfg.sessionCacheEnabled = true;
  cfg.sessionContext = "async-server";

  // we'll use our own internal session cache instead of openssl's
  wangle::SSLCacheOptions cacheOpts;
  cacheOpts.sslCacheTimeout = std::chrono::seconds(kSessionLifeTime);
  // defaults from wangle/acceptor/ServerSocketConfig.h
  cacheOpts.maxSSLCacheSize = 20480;
  cacheOpts.sslCacheFlushSize = 200;
  auto sslContext = std::make_shared<wangle::ServerSSLContext>();
  if (!configureSSLContext(*sslContext, pemCertPath, pemKeyPath, pemCaPath)) {
    return nullptr;
  }
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  // ServerSSLContext handles null
  sslContext->setupTicketManager(ticketKeySeeds.get_pointer(), cfg, nullptr);
#endif
  sslContext->setupSessionCache(
      cfg,
      cacheOpts,
      nullptr, // external cache
      "async-server", // session context
      nullptr); // SSL Stats

#ifdef SSL_CTRL_SET_MAX_SEND_FRAGMENT
  // reduce send fragment size
  SSL_CTX_set_max_send_fragment(sslContext->getSSLCtx(), 8000);
#endif
  return sslContext;
}

std::shared_ptr<SSLContext> createClientSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath) {
  auto context = std::make_shared<ClientSSLContext>(ticketCache.get());
  if (!configureSSLContext(*context, pemCertPath, pemKeyPath, pemCaPath)) {
    return nullptr;
  }
  return context;
}

} // anonymous

std::shared_ptr<SSLContext> getSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    folly::Optional<wangle::TLSTicketKeySeeds> ticketKeySeeds,
    bool clientContext) {
  static constexpr std::chrono::minutes kSslReloadInterval{30};
  thread_local std::unordered_map<CertPaths, ContextInfo, CertPathsHasher>
      localContexts;

  CertPaths paths;
  paths.pemCertPath = pemCertPath;
  paths.pemKeyPath = pemKeyPath;
  paths.pemCaPath = pemCaPath;
  paths.isClient = clientContext;

  auto iter = localContexts.find(paths);
  if (localContexts.find(paths) == localContexts.end()) {
    // Copy strings.
    ContextInfo info;
    info.pemCertPath = pemCertPath.toString();
    info.pemKeyPath = pemKeyPath.toString();
    info.pemCaPath = pemCaPath.toString();
    // Point all StringPiece's to our own strings.
    paths.pemCertPath = info.pemCertPath;
    paths.pemKeyPath = info.pemKeyPath;
    paths.pemCaPath = info.pemCaPath;
    iter = localContexts.insert(std::make_pair(paths, std::move(info))).first;
  }

  auto& contextInfo = iter->second;

  auto now = std::chrono::steady_clock::now();
  if (contextInfo.context == nullptr ||
      now - contextInfo.lastLoadTime > kSslReloadInterval) {
    auto updated = clientContext
        ? createClientSSLContext(pemCertPath, pemKeyPath, pemCaPath)
        : createServerSSLContext(
              pemCertPath, pemKeyPath, pemCaPath, std::move(ticketKeySeeds));
    if (updated) {
      contextInfo.lastLoadTime = now;
      contextInfo.context = std::move(updated);
    }
  }

  return contextInfo.context;
}

} // memcache
} // facebook
