/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/Function.h>
#include <folly/io/async/AsyncSSLSocket.h>

namespace facebook {
namespace memcache {
/**
 * A utility class for SSL related items in McRouter clients and servers.
 * Manages app-specific SSL routines that are used by clients and servers during
 * and immediately after SSL handshakes.
 */
class McSSLUtil {
 public:
  using SSLVerifyFunction =
      folly::Function<bool(folly::AsyncSSLSocket*, bool, X509_STORE_CTX*)
                          const noexcept>;
  using SSLFinalizeFunction =
      folly::Function<void(folly::AsyncTransportWrapper*) const noexcept>;

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
   * Install an app specific SSL finalizer for the server.  This function will
   * be called from multiple threads, so it must be threadsafe.
   * This function should be called once, typically around application init and
   * before the server has received any requests.
   */
  static void setApplicationServerSSLFinalizer(SSLFinalizeFunction func);

  /**
   * Verify an SSL connection.  If no application verifier is set, the default
   * verifier is used.
   */
  static bool verifySSL(folly::AsyncSSLSocket*, bool, X509_STORE_CTX*) noexcept;

  /**
   * Finalize a server SSL connection. Use this to do any processing on the
   * transport after the connection has been accepted.
   */
  static void finalizeServerSSL(folly::AsyncTransportWrapper*) noexcept;
};
} // namespace memcache
} // namespace facebook
