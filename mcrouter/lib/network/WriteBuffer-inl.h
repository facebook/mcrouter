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

  typeId_ = IdFromType<Reply, CarbonMessageList>::value;

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
          std::move(reply),
          ctx_->reqid_,
          ctx_->compressionContext_->codecIdRange,
          ctx_->compressionContext_->compressionCodecMap,
          iovsBegin_,
          iovsCount_);
      break;

    default:
      CHECK(false);
  }
  return false;
}

}} // facebook::memcache
