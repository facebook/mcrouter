/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Function.h>
#include <folly/io/async/AsyncSSLSocket.h>

namespace facebook {
namespace memcache {
/**
 * A utility class for SSL related items in McRouter Servers.
 * Manages an app specific SSL verification routine that is used by
 * McServerSession when verifying SSL handshakes.
 */
class McServerSSLUtil {
 public:
  using SSLVerifyFunction =
      folly::Function<bool(folly::AsyncSSLSocket*, bool, X509_STORE_CTX*)
                          const noexcept>;

  static bool verifySSLWithDefaultBehavior(
      folly::AsyncSSLSocket*,
      bool,
      X509_STORE_CTX*) noexcept;

  /**
   * Install an app specific SSL verifier.  This function will be called
   * from multiple threads, so it must be threadsafe.
   * This function should be called once, typically around application init and
   * before the server has received any requests.
   */
  static void setApplicationSSLVerifier(SSLVerifyFunction func);

  /**
   * Verify an SSL connection.  If no application verifier is set, the default
   * verifier is used.
   */
  static bool verifySSL(folly::AsyncSSLSocket*, bool, X509_STORE_CTX*) noexcept;
};
} // namespace memcache
} // namespace facebook
