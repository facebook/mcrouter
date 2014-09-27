/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ThreadLocalSSLContextProvider.h"

#include <thrift/lib/cpp/transport/TSSLSocket.h>

using apache::thrift::transport::SSLContext;

namespace facebook { namespace memcache {

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
    try {
      auto sslContext = std::make_shared<SSLContext>();
      sslContext->loadCertificate(pemCertPath.begin());
      sslContext->loadPrivateKey(pemKeyPath.begin());
      sslContext->loadTrustedCertificates(pemCaPath.begin());
      sslContext->loadClientCAList(pemCaPath.begin());
      // Disable compression if possible to reduce CPU and memory usage.
#ifdef SSL_OP_NO_COMPRESSION
      sslContext->setOptions(SSL_OP_NO_COMPRESSION);
#endif
      contextInfo.lastLoadTime = now;
      contextInfo.context = std::move(sslContext);
    } catch (const apache::thrift::transport::TTransportException& ex) {
      LOG(ERROR) << "Failed to load certificate, ex: " << ex.what();
    }
  }
  return contextInfo.context;
}

}}  // facebook::memcache
