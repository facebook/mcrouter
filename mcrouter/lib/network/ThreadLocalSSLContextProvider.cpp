/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ThreadLocalSSLContextProvider.h"

#include <unordered_map>

#include <folly/io/async/SSLContext.h>
#include <wangle/ssl/TLSTicketKeyManager.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

using folly::SSLContext;

namespace facebook { namespace memcache {

namespace {

/* Sessions are valid for upto 30 mins */
constexpr size_t kSessionLifeTime = 1800;

struct CertPaths {
  folly::StringPiece pemCertPath;
  folly::StringPiece pemKeyPath;
  folly::StringPiece pemCaPath;

  bool operator==(const CertPaths& other) const {
    return pemCertPath == other.pemCertPath &&
           pemKeyPath == other.pemKeyPath &&
           pemCaPath == other.pemCaPath;
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
    return folly::hash::hash_combine_generic<folly::hash::StdHasher>(
      paths.pemCertPath.hash(), paths.pemKeyPath.hash(),
      paths.pemCaPath.hash());
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

void logCertFailure(folly::StringPiece name, folly::StringPiece path,
                    const std::exception& ex) {
  LOG_FAILURE("SSLCert", failure::Category::kBadEnvironment,
              "Failed to load {} from \"{}\", ex: {}", name, path, ex.what());
}

std::shared_ptr<SSLContext> handleSSLCertsUpdate(folly::StringPiece pemCertPath,
                                                 folly::StringPiece pemKeyPath,
                                                 folly::StringPiece pemCaPath) {

  std::shared_ptr<SSLContext> sslContext(new SSLContext(), SSLContextDeleter());

  // Load certificate.
  try {
    sslContext->loadCertificate(pemCertPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("certificate", pemCertPath, ex);
    return nullptr;
  }

  // Load private key.
  try {
    sslContext->loadPrivateKey(pemKeyPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("private key", pemKeyPath, ex);
    return nullptr;
  }

  // Load trusted certificates.
  try {
    sslContext->loadTrustedCertificates(pemCaPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("trusted certificates", pemCaPath, ex);
    return nullptr;
  }

  // Load client CA list.
  try {
    sslContext->loadClientCAList(pemCaPath.begin());
  } catch (const std::exception& ex) {
    logCertFailure("client CA list", pemCaPath, ex);
    return nullptr;
  }

  // Try to disable compression if possible to reduce CPU and memory usage.
#ifdef SSL_OP_NO_COMPRESSION
  try {
    sslContext->setOptions(SSL_OP_NO_COMPRESSION);
  } catch (const std::runtime_error& ex) {
    LOG_FAILURE("SSLCert", failure::Category::kSystemError,
                "Failed to apply SSL_OP_NO_COMPRESSION flag onto SSLContext "
                "with files: pemCertPath='{}', pemKeyPath='{}', pemCaPath='{}'",
                pemCertPath, pemKeyPath, pemCaPath);
    // We failed to disable compression, but the SSLContext itself is good to
    // use.
  }
#endif
  return sslContext;
}
}  // anonymous

std::shared_ptr<SSLContext> getSSLContext(folly::StringPiece pemCertPath,
                                          folly::StringPiece pemKeyPath,
                                          folly::StringPiece pemCaPath) {
  static constexpr std::chrono::minutes kSslReloadInterval{30};
  thread_local std::unordered_map<CertPaths, ContextInfo, CertPathsHasher>
  localContexts;

  CertPaths paths;
  paths.pemCertPath = pemCertPath;
  paths.pemKeyPath = pemKeyPath;
  paths.pemCaPath = pemCaPath;

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

    if (auto updated =
            handleSSLCertsUpdate(pemCertPath, pemKeyPath, pemCaPath)) {
      contextInfo.lastLoadTime = now;
      contextInfo.context = std::move(updated);
      contextInfo.context->setSessionCacheContext("async-server");
      SSL_CTX_set_timeout(contextInfo.context->getSSLCtx(), kSessionLifeTime);

      #ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
      auto mgr = folly::make_unique<wangle::TLSTicketKeyManager>(
          contextInfo.context.get(), nullptr);
      mgr->setTLSTicketKeySeeds({"aaaa"}, {"bbbb"}, {"cccc"});

      /* store in the map */
      const auto mapKey =
          reinterpret_cast<uintptr_t>(contextInfo.context.get());
      assert(contextToTicketMgr()->find(mapKey) == contextToTicketMgr()->end());
      contextToTicketMgr()->emplace(mapKey, std::move(mgr));
      #endif
    }
  }

  return contextInfo.context;
}

}}  // facebook::memcache
