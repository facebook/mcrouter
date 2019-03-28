/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include "FizzContextProvider.h"

#include <fizz/client/FizzClientContext.h>
#include <fizz/client/SynchronizedLruPskCache.h>
#include <fizz/protocol/DefaultCertificateVerifier.h>
#include <fizz/server/FizzServerContext.h>
#include <fizz/server/TicketCodec.h>
#include <fizz/server/TicketTypes.h>
#include <folly/Singleton.h>
#include <folly/ssl/Init.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook {
namespace memcache {

namespace {
void initSSL() {
  static folly::once_flag flag;
  folly::call_once(flag, [&]() { folly::ssl::init(); });
}

/* Sessions are valid for upto 24 hours */
constexpr size_t kSessionLifeTime = 86400;
} // namespace

FizzContextAndVerifier createClientFizzContextAndVerifier(
    std::string certData,
    std::string keyData,
    folly::StringPiece pemCaPath) {
  // global session cache
  static auto SESSION_CACHE =
      std::make_shared<fizz::client::SynchronizedLruPskCache>(100);
  initSSL();
  auto ctx = std::make_shared<fizz::client::FizzClientContext>();
  ctx->setSupportedVersions({fizz::ProtocolVersion::tls_1_3});
  ctx->setPskCache(SESSION_CACHE);

  if (!certData.empty() && !keyData.empty()) {
    auto cert =
        fizz::CertUtils::makeSelfCert(std::move(certData), std::move(keyData));
    ctx->setClientCertificate(std::move(cert));
  }
  std::shared_ptr<fizz::DefaultCertificateVerifier> verifier;
  if (!pemCaPath.empty()) {
    verifier = fizz::DefaultCertificateVerifier::createFromCAFile(
        fizz::VerificationContext::Client, pemCaPath.str());
  }
  return FizzContextAndVerifier(std::move(ctx), std::move(verifier));
}

std::shared_ptr<fizz::server::FizzServerContext> createFizzServerContext(
    folly::StringPiece pemCertPath,
    folly::StringPiece certData,
    folly::StringPiece pemKeyPath,
    folly::StringPiece keyData,
    folly::StringPiece pemCaPath,
    bool requireClientVerification,
    wangle::TLSTicketKeySeeds* ticketKeySeeds) {
  initSSL();
  auto certMgr = std::make_unique<fizz::server::CertManager>();
  try {
    auto selfCert =
        fizz::CertUtils::makeSelfCert(certData.str(), keyData.str());
    // add the default cert
    certMgr->addCert(std::move(selfCert), true);
  } catch (const std::exception& ex) {
    LOG_FAILURE(
        "SSLCert",
        failure::Category::kBadEnvironment,
        "Failed to create self cert from \"{}\" and \"{}\".  ex: {}",
        pemCertPath,
        pemKeyPath,
        ex.what());
    return nullptr;
  }

  auto ctx = std::make_shared<fizz::server::FizzServerContext>();
  ctx->setSupportedVersions({fizz::ProtocolVersion::tls_1_3});
  ctx->setSupportedPskModes(
      {fizz::PskKeyExchangeMode::psk_ke, fizz::PskKeyExchangeMode::psk_dhe_ke});
  ctx->setVersionFallbackEnabled(true);
  ctx->setCertManager(std::move(certMgr));
  if (!pemCaPath.empty()) {
    auto verifier = fizz::DefaultCertificateVerifier::createFromCAFile(
        fizz::VerificationContext::Server, pemCaPath.str());
    ctx->setClientCertVerifier(std::move(verifier));
    ctx->setClientAuthMode(fizz::server::ClientAuthMode::Optional);
  }
  if (requireClientVerification) {
    ctx->setClientAuthMode(fizz::server::ClientAuthMode::Required);
  }

  // set ticket seeds
  if (ticketKeySeeds) {
    std::vector<folly::ByteRange> ticketSecrets;
    for (const auto& secret : ticketKeySeeds->currentSeeds) {
      ticketSecrets.push_back(folly::StringPiece(secret));
    }
    for (const auto& secret : ticketKeySeeds->oldSeeds) {
      ticketSecrets.push_back(folly::StringPiece(secret));
    }
    for (const auto& secret : ticketKeySeeds->newSeeds) {
      ticketSecrets.push_back(folly::StringPiece(secret));
    }
    auto cipher = std::make_shared<fizz::server::AES128TicketCipher>();
    cipher->setTicketSecrets(std::move(ticketSecrets));
    cipher->setValidity(std::chrono::seconds(kSessionLifeTime));
    ctx->setTicketCipher(std::move(cipher));
  }
  // TODO: allow for custom FizzFactory
  return ctx;
}
} // namespace memcache
} // namespace facebook
