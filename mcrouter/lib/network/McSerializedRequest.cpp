/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McSerializedRequest.h"

#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

McSerializedRequest::~McSerializedRequest() {
  switch (protocol_) {
    case mc_ascii_protocol:
      asciiRequest_.~AsciiSerializedRequest();
      break;
    case mc_caret_protocol:
      caretRequest_.~CaretSerializedMessage();
      break;
    case mc_umbrella_protocol:
      umbrellaMessage_.~UmbrellaSerializedMessage();
      break;
    case mc_unknown_protocol:
    case mc_binary_protocol:
    case mc_nprotocols:
      break;
  }
}

bool McSerializedRequest::checkKeyLength(const folly::IOBuf& key) {
  if (key.computeChainDataLength() > MC_KEY_MAX_LEN_UMBRELLA) {
    result_ = Result::BAD_KEY;
    return false;
  }
  return true;
}

McSerializedRequest::Result McSerializedRequest::serializationResult() const {
  return result_;
}

}} // facebook::memcache
