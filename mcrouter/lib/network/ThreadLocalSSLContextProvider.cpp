/*
 *  Copyright (c) 2015, Facebook, Inc.
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

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

using folly::SSLContext;

namespace facebook { namespace memcache {

namespace {
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

struct ContextInfo {
  std::string pemCertPath;
  std::string pemKeyPath;
  std::string pemCaPath;

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

void logCertFailure(folly::StringPiece name, folly::StringPiece path,
                    const std::exception& ex) {
  LOG_FAILURE("SSLCert", failure::Category::kBadEnvironment,
              "Failed to load {} from \"{}\", ex: {}", name, path, ex.what());
}

std::shared_ptr<SSLContext> handleSSLCertsUpdate(folly::StringPiece pemCertPath,
                                                 folly::StringPiece pemKeyPath,
                                                 folly::StringPiece pemCaPath) {
  auto sslContext = std::make_shared<SSLContext>();

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
  static constexpr std::chrono::minutes kSslReloadInterval{5};
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
    }
  }
  return contextInfo.context;
}

}}  // facebook::memcache
