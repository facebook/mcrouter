/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "ThreadLocalSSLContextProvider.h"

#include <unordered_map>

#include <folly/Singleton.h>
#include <folly/hash/Hash.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/SSLOptions.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/Init.h>
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
#include "mcrouter/lib/network/SecurityOptions.h"

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
  bool requireClientVerification{false};
  SecurityMech mech{SecurityMech::TLS};

  bool operator==(const ContextKey& other) const {
    return pemCertPath == other.pemCertPath && pemKeyPath == other.pemKeyPath &&
        pemCaPath == other.pemCaPath &&
        requireClientVerification == other.requireClientVerification &&
        mech == other.mech;
  }
};

struct ClientContextInfo {
  std::string pemCertPath;
  std::string pemKeyPath;
  std::string pemCaPath;
  SecurityMech mech;

  std::shared_ptr<SSLContext> context;
  FizzContextAndVerifier fizzData;
  std::chrono::time_point<std::chrono::steady_clock> lastLoadTime;

  bool needsContext(
      std::chrono::time_point<std::chrono::steady_clock> now) const {
    constexpr auto kSslReloadInterval = std::chrono::minutes(30);
    if (!context) {
      return true;
    }
    return now - lastLoadTime > kSslReloadInterval;
  }

  bool needsFizzContext(
      std::chrono::time_point<std::chrono::steady_clock> now) const {
    constexpr auto kSslReloadInterval = std::chrono::minutes(30);
    if (!fizzData.first) {
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

  void setFizzData(
      FizzContextAndVerifier ctxAndVerifier,
      std::chrono::time_point<std::chrono::steady_clock> loadTime) {
    if (ctxAndVerifier.first) {
      fizzData = std::move(ctxAndVerifier);
      lastLoadTime = loadTime;
    }
  }
};

struct ServerContextInfo {
  std::string pemCertPath;
  std::string pemKeyPath;
  std::string pemCaPath;

  std::shared_ptr<SSLContext> context;
  std::shared_ptr<fizz::server::FizzServerContext> fizzContext;
  std::chrono::time_point<std::chrono::steady_clock> lastLoadTime;

  bool needsContexts(
      std::chrono::time_point<std::chrono::steady_clock> now) const {
    constexpr auto kSslReloadInterval = std::chrono::minutes(30);
    if (!context || !fizzContext) {
      return true;
    }
    return now - lastLoadTime > kSslReloadInterval;
  }

  void setContexts(
      std::shared_ptr<SSLContext> ctx,
      std::shared_ptr<fizz::server::FizzServerContext> fizzCtx,
      std::chrono::time_point<std::chrono::steady_clock> loadTime) {
    if (ctx && fizzCtx) {
      context = std::move(ctx);
      fizzContext = std::move(fizzCtx);
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
        key.mech);
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

bool configureServerSSLContext(
    folly::SSLContext& sslContext,
    folly::StringPiece pemCertPath,
    folly::StringPiece certData,
    folly::StringPiece pemKeyPath,
    folly::StringPiece keyData,
    folly::StringPiece pemCaPath) {
  // Load certificate.
  try {
    sslContext.loadCertificateFromBufferPEM(certData);
  } catch (const std::exception& ex) {
    logCertFailure("certificate", pemCertPath, ex);
    return false;
  }

  // Load private key.
  try {
    sslContext.loadPrivateKeyFromBufferPEM(keyData);
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

  folly::ssl::setCipherSuites<folly::ssl::SSLServerOptions>(sslContext);

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
  auto cacheLayer = std::make_shared<TicketCacheLayer>(
      wangle::PersistentCacheConfig::Builder().setCapacity(100).build());
  cacheLayer->init();
  return new SSLTicketCache(std::move(cacheLayer));
});

std::shared_ptr<SSLContext> createServerSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece certData,
    folly::StringPiece pemKeyPath,
    folly::StringPiece keyData,
    folly::StringPiece pemCaPath,
    bool requireClientVerification,
    wangle::TLSTicketKeySeeds* ticketKeySeeds) {
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
  if (!configureServerSSLContext(
          *sslContext, pemCertPath, certData, pemKeyPath, keyData, pemCaPath)) {
    return nullptr;
  }
  sslContext->setServerECCurve("prime256v1");
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  // ServerSSLContext handles null
  sslContext->setupTicketManager(ticketKeySeeds, cfg, nullptr);
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
#if FOLLY_OPENSSL_HAS_ALPN
  // servers can always negotiate this - it is up to the client to do so.
  sslContext->setAdvertisedNextProtocols(
      {kMcSecurityTlsToPlaintextProto.str()});
#endif
  return sslContext;
}

std::string readFile(folly::StringPiece path) {
  std::string data;
  if (path.empty()) {
    return data;
  }
  if (!folly::readFile(path.begin(), data)) {
    LOG_FAILURE(
        "SSLCert",
        failure::Category::kBadEnvironment,
        "Failed to load file from \"{}\"",
        path);
  }
  return data;
}

std::shared_ptr<SSLContext> createClientSSLContext(
    SecurityOptions opts,
    SecurityMech mech) {
  auto context = std::make_shared<ClientSSLContext>(ticketCache.get());
  // note we use setCipherSuites instead of setClientOptions since client
  // options will enable false start by default.
  folly::ssl::setCipherSuites<folly::ssl::SSLCommonOptions>(*context);
  const auto& pemCertPath = opts.sslPemCertPath;
  const auto& pemKeyPath = opts.sslPemKeyPath;
  const auto& pemCaPath = opts.sslPemCaPath;
  if (!pemCertPath.empty() && !pemKeyPath.empty()) {
    try {
      context->loadCertificate(pemCertPath.c_str());
    } catch (const std::exception& ex) {
      logCertFailure("certificate", pemCertPath, ex);
      return nullptr;
    }
    // Load private key.
    try {
      context->loadPrivateKey(pemKeyPath.c_str());
    } catch (const std::exception& ex) {
      logCertFailure("private key", pemKeyPath, ex);
      return nullptr;
    }
  }
  if (!pemCaPath.empty()) {
    // we are going to verify server certificates
    context->loadTrustedCertificates(pemCaPath.c_str());
    // only verify that the server is trusted - no peer name verification yet
    context->authenticate(true, false);
    context->setVerificationOption(
        folly::SSLContext::SSLVerifyPeerEnum::VERIFY);
  }
#if FOLLY_OPENSSL_HAS_ALPN
  if (mech == SecurityMech::TLS_TO_PLAINTEXT) {
    context->setAdvertisedNextProtocols({kMcSecurityTlsToPlaintextProto.str()});
  }
#endif
  return context;
}

ClientContextInfo& getClientContextInfo(
    const SecurityOptions& opts,
    SecurityMech mech) {
  thread_local std::
      unordered_map<ContextKey, ClientContextInfo, ContextKeyHasher>
          localContexts;

  ContextKey key;
  key.pemCertPath = opts.sslPemCertPath;
  key.pemKeyPath = opts.sslPemKeyPath;
  key.pemCaPath = opts.sslPemCaPath;
  key.mech = mech;

  auto iter = localContexts.find(key);
  if (iter == localContexts.end()) {
    // Copy strings.
    ClientContextInfo info;
    info.pemCertPath = opts.sslPemCertPath;
    info.pemKeyPath = opts.sslPemKeyPath;
    info.pemCaPath = opts.sslPemCaPath;
    info.mech = mech;

    // Point all StringPiece's to our own strings.
    key.pemCertPath = info.pemCertPath;
    key.pemKeyPath = info.pemKeyPath;
    key.pemCaPath = info.pemCaPath;
    iter = localContexts.insert(std::make_pair(key, std::move(info))).first;
  }

  return iter->second;
}

ServerContextInfo& getServerContextInfo(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    bool requireClientVerification) {
  thread_local std::
      unordered_map<ContextKey, ServerContextInfo, ContextKeyHasher>
          localContexts;

  ContextKey key;
  key.pemCertPath = pemCertPath;
  key.pemKeyPath = pemKeyPath;
  key.pemCaPath = pemCaPath;
  key.requireClientVerification = requireClientVerification;

  auto iter = localContexts.find(key);
  if (iter == localContexts.end()) {
    // Copy strings.
    ServerContextInfo info;
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

bool sslContextsAreThreadSafe() {
  static folly::once_flag flag;
  static bool ctxLockDisabled = false;
  folly::call_once(flag, [&] {
    folly::ssl::init();
#ifdef CRYPTO_LOCK_SSL_CTX
    ctxLockDisabled = folly::ssl::isLockDisabled(CRYPTO_LOCK_SSL_CTX);
#endif
  });
  return !ctxLockDisabled;
}

FizzContextAndVerifier getFizzClientConfig(const SecurityOptions& opts) {
  auto& info = getClientContextInfo(opts, SecurityMech::TLS13_FIZZ);
  auto now = std::chrono::steady_clock::now();
  if (info.needsFizzContext(now)) {
    auto certData = readFile(opts.sslPemCertPath);
    auto keyData = readFile(opts.sslPemKeyPath);
    auto fizzData = createClientFizzContextAndVerifier(
        std::move(certData), std::move(keyData), opts.sslPemCaPath);
    info.setFizzData(std::move(fizzData), now);
  }
  return info.fizzData;
}

std::shared_ptr<folly::SSLContext> getClientContext(
    const SecurityOptions& opts,
    SecurityMech mech) {
  if (mech != SecurityMech::TLS && mech != SecurityMech::TLS_TO_PLAINTEXT) {
    LOG_FAILURE(
        "SSLConfig",
        failure::Category::kInvalidOption,
        "getClientContext specified invalid security mech: {}",
        static_cast<uint8_t>(mech));
    return nullptr;
  }
  auto& info = getClientContextInfo(opts, mech);
  auto now = std::chrono::steady_clock::now();
  if (info.needsContext(now)) {
    auto ctx = createClientSSLContext(opts, mech);
    info.setContext(std::move(ctx), now);
  }
  return info.context;
}

ServerContextPair getServerContexts(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    bool requireClientCerts,
    folly::Optional<wangle::TLSTicketKeySeeds> seeds) {
  auto& info = getServerContextInfo(
      pemCertPath, pemKeyPath, pemCaPath, requireClientCerts);
  auto now = std::chrono::steady_clock::now();
  if (info.needsContexts(now)) {
    auto certData = readFile(pemCertPath);
    auto keyData = readFile(pemKeyPath);
    auto ctx = createServerSSLContext(
        pemCertPath,
        certData,
        pemKeyPath,
        keyData,
        pemCaPath,
        requireClientCerts,
        seeds.get_pointer());
    auto fizzCtx = createFizzServerContext(
        pemCertPath,
        certData,
        pemKeyPath,
        keyData,
        pemCaPath,
        requireClientCerts,
        seeds.get_pointer());
    info.setContexts(std::move(ctx), std::move(fizzCtx), now);
  }
  return ServerContextPair(info.context, info.fizzContext);
}

} // memcache
} // facebook
