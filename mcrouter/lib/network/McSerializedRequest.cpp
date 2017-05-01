/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McSerializedRequest.h"

namespace facebook {
namespace memcache {

McSerializedRequest::~McSerializedRequest() {
  switch (protocol_) {
    case mc_ascii_protocol:
      asciiRequest_.~AsciiSerializedRequest();
      break;
    case mc_caret_protocol:
      caretRequest_.~CaretSerializedMessage();
      break;
    case mc_umbrella_protocol_DONOTUSE:
      umbrellaMessage_.~UmbrellaSerializedMessage();
      break;
    case mc_unknown_protocol:
    case mc_binary_protocol:
    case mc_nprotocols:
      break;
  }
}
}
} // facebook::memcache
