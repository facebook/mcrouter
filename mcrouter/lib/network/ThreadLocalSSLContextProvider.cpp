/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
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

struct ContextKey {
  folly::StringPiece pemCertPath;
  folly::StringPiece pemKeyPath;
  folly::StringPiece pemCaPath;
  bool requireClientVerification;
  bool isClient;

  bool operator==(const ContextKey& other) const {
    return pemCertPath == other.pemCertPath && pemKeyPath == other.pemKeyPath &&
        pemCaPath == other.pemCaPath &&
        requireClientVerification == other.requireClientVerification &&
        isClient == other.isClient;
  }
};

struct ContextInfo {
  std::string pemCertPath;
  std::string pemKeyPath;
  std::string pemCaPath;

  std::shared_ptr<SSLContext> context;
  std::chrono::time_point<std::chrono::steady_clock> lastLoadTime;

  bool needsContext(
      std::chrono::time_point<std::chrono::steady_clock> now) const {
    constexpr auto kSslReloadInterval = std::chrono::minutes(30);
    if (!context) {
      return true;
    }
    return now - lastLoadTime > kSslReloadInterval;
  }

  void setContext(
      std::shared_ptr<SSLContext> ctx,
      std::chrono::time_point<std::chrono::steady_clock> loadTime) {
    if (ctx) {
      context = std::move(ctx);
      lastLoadTime = loadTime;
    }
  }
};

struct ContextKeyHasher {
  size_t operator()(const ContextKey& key) const {
    return folly::Hash()(
        key.pemCertPath,
        key.pemKeyPath,
        key.pemCaPath,
        key.requireClientVerification,
        key.isClient);
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
    bool requireClientVerification,
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
  sslContext->setServerECCurve("prime256v1");
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
  if (requireClientVerification) {
    sslContext->setVerificationOption(
        folly::SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT);
  } else {
    // request client certs and verify them if the client presents them.
    sslContext->setVerificationOption(
        folly::SSLContext::SSLVerifyPeerEnum::VERIFY);
  }
  return sslContext;
}

std::shared_ptr<SSLContext> createClientSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath) {
  auto context = std::make_shared<ClientSSLContext>(ticketCache.get());
  if (!pemCertPath.empty() && !pemKeyPath.empty()) {
    try {
      context->loadCertificate(pemCertPath.begin());
    } catch (const std::exception& ex) {
      logCertFailure("certificate", pemCertPath, ex);
      return nullptr;
    }
    // Load private key.
    try {
      context->loadPrivateKey(pemKeyPath.begin());
    } catch (const std::exception& ex) {
      logCertFailure("private key", pemKeyPath, ex);
      return nullptr;
    }
  }
  if (!pemCaPath.empty()) {
    // we are going to verify server certificates
    context->loadTrustedCertificates(pemCaPath.begin());
    // only verify that the server is trusted - no peer name verification yet
    context->authenticate(true, false);
    context->setVerificationOption(
        folly::SSLContext::SSLVerifyPeerEnum::VERIFY);
  }
  return context;
}

ContextInfo& getContextInfo(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    bool requireClientVerification,
    bool isClient) {
  thread_local std::unordered_map<ContextKey, ContextInfo, ContextKeyHasher>
      localContexts;

  ContextKey key;
  key.pemCertPath = pemCertPath;
  key.pemKeyPath = pemKeyPath;
  key.pemCaPath = pemCaPath;
  key.requireClientVerification = requireClientVerification;
  key.isClient = isClient;

  auto iter = localContexts.find(key);
  if (iter == localContexts.end()) {
    // Copy strings.
    ContextInfo info;
    info.pemCertPath = pemCertPath.toString();
    info.pemKeyPath = pemKeyPath.toString();
    info.pemCaPath = pemCaPath.toString();

    // Point all StringPiece's to our own strings.
    key.pemCertPath = info.pemCertPath;
    key.pemKeyPath = info.pemKeyPath;
    key.pemCaPath = info.pemCaPath;
    iter = localContexts.insert(std::make_pair(key, std::move(info))).first;
  }

  return iter->second;
}

} // namespace

std::shared_ptr<folly::SSLContext> getClientContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath) {
  auto& info = getContextInfo(pemCertPath, pemKeyPath, pemCaPath, false, true);
  auto now = std::chrono::steady_clock::now();
  if (info.needsContext(now)) {
    auto ctx = createClientSSLContext(pemCertPath, pemKeyPath, pemCaPath);
    info.setContext(std::move(ctx), now);
  }
  return info.context;
}

std::shared_ptr<folly::SSLContext> getServerContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    bool requireClientCerts,
    folly::Optional<wangle::TLSTicketKeySeeds> seeds) {
  auto& info = getContextInfo(
      pemCertPath, pemKeyPath, pemCaPath, requireClientCerts, false);
  auto now = std::chrono::steady_clock::now();
  if (info.needsContext(now)) {
    auto ctx = createServerSSLContext(
        pemCertPath,
        pemKeyPath,
        pemCaPath,
        requireClientCerts,
        std::move(seeds));
    info.setContext(std::move(ctx), now);
  }
  return info.context;
}

} // memcache
} // facebook
