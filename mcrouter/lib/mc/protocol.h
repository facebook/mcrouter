/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

#define MC_KEY_MAX_LEN_ASCII (250)
#define MC_KEY_MAX_LEN_UMBRELLA (2 * 1024)
#define MC_KEY_MAX_LEN (MC_KEY_MAX_LEN_UMBRELLA)

typedef enum mc_protocol_e {
  mc_unknown_protocol = 0,
  mc_ascii_protocol = 1,
  mc_binary_protocol = 2,
  mc_umbrella_protocol_DONOTUSE = 3, /* New code should use Caret or ASCII */
  mc_caret_protocol = 4,
  mc_nprotocols, // placeholder
} mc_protocol_t;

static inline mc_protocol_t mc_string_to_protocol(const char* str) {
  if (!strcmp(str, "ascii")) {
    return mc_ascii_protocol;
  } else if (!strcmp(str, "binary")) {
    return mc_binary_protocol;
  } else if (!strcmp(str, "umbrella")) {
    return mc_umbrella_protocol_DONOTUSE;
  } else if (!strcmp(str, "caret")) {
    return mc_caret_protocol;
  } else {
    return mc_unknown_protocol;
  }
}

static inline const char* mc_protocol_to_string(const mc_protocol_t value) {
  static const char* const strings[] = {
      "unknown-protocol", "ascii", "binary", "umbrella", "caret",
  };
  return strings[value < mc_nprotocols ? value : mc_unknown_protocol];
}

__END_DECLS
