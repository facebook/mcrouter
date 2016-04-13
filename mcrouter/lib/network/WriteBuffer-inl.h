/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/network/CaretSerializedMessage.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

template <class Arg1, class Arg2>
void AsciiSerializedReply::addStrings(Arg1&& arg1, Arg2&& arg2) {
  addString(std::forward<Arg1>(arg1));
  addString(std::forward<Arg2>(arg2));
}

template <class Arg, class... Args>
void AsciiSerializedReply::addStrings(Arg&& arg, Args&&... args) {
  addString(std::forward<Arg>(arg));
  addStrings(std::forward<Args>(args)...);
}

template <class Reply>
bool WriteBuffer::prepareTyped(
    McServerRequestContext&& ctx,
    Reply&& reply,
    Destructor destructor) {
  ctx_.emplace(std::move(ctx));
  assert(!destructor_.hasValue());
  if (destructor) {
    destructor_ = std::move(destructor);
  }

  switch (protocol_) {
    case mc_ascii_protocol:
      return asciiReply_.prepare(
          std::move(reply), ctx_->asciiKey(), iovsBegin_, iovsCount_);
      break;

    case mc_umbrella_protocol:
      return umbrellaReply_.prepare(
          std::move(reply), ctx_->reqid_, iovsBegin_, iovsCount_);
      break;

    case mc_caret_protocol:
      return caretReply_.prepare(
          std::move(reply), ctx_->reqid_, iovsBegin_, iovsCount_);
      break;

    default:
      CHECK(false);
  }
  return false;
}

}} // facebook::memcache
