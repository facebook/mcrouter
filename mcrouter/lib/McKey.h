/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <ctype.h>

#include <folly/Range.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"

namespace facebook {
namespace memcache {

/**
 * Checks whether the given memcache key is valid.
 * The key must satisfy:
 *   1) The length should be nonzero.
 *   2) The length should be at most MC_KEY_MAX_LEN.
 *   3) There should be no spaces or control characters.
 */
inline mc_req_err_t isKeyValid(folly::StringPiece key) {
  if (key.empty()) {
    return mc_req_err_no_key;
  }

  if (key.size() > MC_KEY_MAX_LEN) {
    return mc_req_err_key_too_long;
  }

  for (auto c : key) {
    // iscntrl(c) || isspace(c)
    if ((unsigned)c <= 0x20 || (unsigned)c == 0x7F) {
      return mc_req_err_space_or_ctrl;
    }
  }

  return mc_req_err_valid;
}
}
}
