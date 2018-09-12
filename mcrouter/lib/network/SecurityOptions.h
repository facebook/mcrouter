/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/Range.h>

namespace facebook {
namespace memcache {

// an enum to determine which security protocol to use to connect to the
// endpoint.
enum class SecurityMech : uint8_t {
  NONE,
  TLS,
};

const char* securityMechToString(SecurityMech mech);
SecurityMech parseSecurityMech(folly::StringPiece s);

struct SecurityOptions {
  /**
   * Certificate paths for mutual auth or server cert verification.
   * If cert and key paths are empty, then no client cert is presented
   * If ca path is empty, then no server cert verification is attempted
   */
  std::string sslPemCertPath;
  std::string sslPemKeyPath;
  std::string sslPemCaPath;

  /**
   * enable ssl session caching
   */
  bool sessionCachingEnabled{false};

  /**
   * enable ssl handshake offload to a separate thread pool
   */
  bool sslHandshakeOffload{false};

  /**
   * Service identity of the destination service when SSL is used.
   */
  std::string sslServiceIdentity;

  /**
   * Whether TFO is enabled for SSL connections
   */
  bool tfoEnabledForSsl{false};
};

} // namespace memcache
} // namespace facebook
