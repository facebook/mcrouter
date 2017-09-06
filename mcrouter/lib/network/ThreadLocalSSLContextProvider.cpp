/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ThreadLocalSSLContextProvider.h"

#include <unordered_map>

#include <folly/Hash.h>
#include <folly/io/async/SSLContext.h>
#include <wangle/ssl/TLSTicketKeyManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

using folly::SSLContext;

namespace facebook {
namespace memcache {

namespace {

/* Sessions are valid for upto 30 mins */
constexpr size_t kSessionLifeTime = 1800;

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

using TicketMgrMap =
    std::unordered_map<uintptr_t, std::unique_ptr<wangle::TLSTicketKeyManager>>;

struct ContextInfo {
  std::string pemCertPath;
  std::string pemKeyPath;
  std::string pemCaPath;

  /* mgrMapRef must be declared before context to control the construction
   * destruction order. */
  std::shared_ptr<TicketMgrMap> mgrMapRef;
  std::shared_ptr<SSLContext> context;

  std::chrono::time_point<std::chrono::steady_clock> lastLoadTime;
};

struct CertPathsHasher {
  size_t operator()(const CertPaths& paths) const {
    return folly::Hash()(
        paths.pemCertPath, paths.pemKeyPath, paths.pemCaPath, paths.isClient);
  }
};

std::shared_ptr<TicketMgrMap> contextToTicketMgr() {
  thread_local auto map = std::make_shared<TicketMgrMap>();
  return map;
}

/* custom deleter to release TLSTicketKeyManager */
struct SSLContextDeleter {
  void operator()(folly::SSLContext* ctx) const {
    if (ctx == nullptr) {
      return;
    }
    const auto mapKey = reinterpret_cast<uintptr_t>(ctx);
    contextToTicketMgr()->erase(mapKey);
    assert(contextToTicketMgr()->find(mapKey) == contextToTicketMgr()->end());
    delete ctx;
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

std::shared_ptr<SSLContext> createServerSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath,
    folly::Optional<wangle::TLSTicketKeySeeds> ticketKeySeeds) {
  std::shared_ptr<SSLContext> sslContext(new SSLContext(), SSLContextDeleter());
  if (!configureSSLContext(*sslContext, pemCertPath, pemKeyPath, pemCaPath)) {
    return nullptr;
  }
  sslContext->setSessionCacheContext("async-server");
  SSL_CTX_set_timeout(sslContext->getSSLCtx(), kSessionLifeTime);

#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  if (ticketKeySeeds.hasValue()) {
    auto mgr = std::make_unique<wangle::TLSTicketKeyManager>(
        sslContext.get(), nullptr);
    mgr->setTLSTicketKeySeeds(
        std::move(ticketKeySeeds->oldSeeds),
        std::move(ticketKeySeeds->currentSeeds),
        std::move(ticketKeySeeds->newSeeds));

    /* store in the map */
    const auto mapKey = reinterpret_cast<uintptr_t>(sslContext.get());
    assert(contextToTicketMgr()->find(mapKey) == contextToTicketMgr()->end());
    contextToTicketMgr()->emplace(mapKey, std::move(mgr));
  }
#endif
  return sslContext;
}

std::shared_ptr<SSLContext> createClientSSLContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece pemKeyPath,
    folly::StringPiece pemCaPath) {
  auto context = std::make_shared<SSLContext>();
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
    info.mgrMapRef = contextToTicketMgr();
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
