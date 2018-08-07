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

} // namespace memcache
} // namespace facebook
