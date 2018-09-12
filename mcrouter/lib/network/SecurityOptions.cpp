/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include "SecurityOptions.h"

#include <exception>

#include <folly/lang/Assume.h>

namespace facebook {
namespace memcache {

const char* securityMechToString(SecurityMech mech) {
  switch (mech) {
    case SecurityMech::NONE:
      return "plain";
    case SecurityMech::TLS:
      return "ssl";
  };
  folly::assume_unreachable();
}

SecurityMech parseSecurityMech(folly::StringPiece s) {
  if (s == "ssl") {
    return SecurityMech::TLS;
  } else if (s == "plain") {
    return SecurityMech::NONE;
  }
  throw std::invalid_argument("Invalid security mech");
}

} // namespace memcache
} // namespace facebook
