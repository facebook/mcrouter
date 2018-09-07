/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include "McSerializedRequest.h"

namespace facebook {
namespace memcache {

size_t McSerializedRequest::getBodySize() {
  switch (protocol_) {
    case mc_ascii_protocol:
      return asciiRequest_.getSize();
    case mc_caret_protocol:
      return caretRequest_.getSizeNoHeader();
    case mc_umbrella_protocol_DONOTUSE:
      return umbrellaMessage_.getSizeNoHeader();
    default:
      // Unreachable, see constructor.
      return 0;
  }
}

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
