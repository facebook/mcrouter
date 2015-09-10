/*
 *  Copyright (c) 2015, Facebook, Inc.
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

namespace facebook {
namespace memcache {

template <class Reply>
bool WriteBuffer::prepareTyped(McServerRequestContext&& ctx,
                               Reply&& reply,
                               size_t typeId) {
  ctx_.emplace(std::move(ctx));

  ensureType(UmbrellaVersion::TYPED_MESSAGE);
  return caretReply_.prepare(
      std::move(reply), ctx_->reqid_, typeId, iovsBegin_, iovsCount_);
}
}
} // facebook::memcache
