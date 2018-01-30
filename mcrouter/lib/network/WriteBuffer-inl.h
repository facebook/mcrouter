/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/network/CaretSerializedMessage.h"
#include "mcrouter/lib/network/CpuController.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/MemoryController.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

template <class Reply>
typename std::enable_if<
    ListContains<
        McRequestList,
        RequestFromReplyType<Reply, RequestReplyPairs>>::value,
    bool>::type
WriteBuffer::prepareTyped(
    McServerRequestContext&& ctx,
    Reply&& reply,
    Destructor destructor,
    const CompressionCodecMap* compressionCodecMap,
    const CodecIdRange& codecIdRange) {
  ctx_.emplace(std::move(ctx));
  assert(!destructor_.hasValue());
  if (destructor) {
    destructor_ = std::move(destructor);
  }

  typeId_ = static_cast<uint32_t>(Reply::typeId);

  // The current congestion control only supports mc_caret_protocol.
  // May extend to other protocals in the future.
  switch (protocol_) {
    case mc_ascii_protocol:
      return asciiReply_.prepare(
          std::move(reply), ctx_->asciiKey(), iovsBegin_, iovsCount_);

    case mc_umbrella_protocol_DONOTUSE:
      return umbrellaReply_.prepare(
          std::move(reply), ctx_->reqid_, iovsBegin_, iovsCount_);

    case mc_caret_protocol:
      return caretReply_.prepare(
          std::move(reply),
          ctx_->reqid_,
          codecIdRange,
          compressionCodecMap,
          ctx_->getDropProbability(),
          ctx_->getServerLoad(),
          iovsBegin_,
          iovsCount_);

    default:
      CHECK(false);
      return false;
  }
}

template <class Reply>
typename std::enable_if<
    !ListContains<
        McRequestList,
        RequestFromReplyType<Reply, RequestReplyPairs>>::value,
    bool>::type
WriteBuffer::prepareTyped(
    McServerRequestContext&& ctx,
    Reply&& reply,
    Destructor destructor,
    const CompressionCodecMap* compressionCodecMap,
    const CodecIdRange& codecIdRange) {
  assert(protocol_ == mc_caret_protocol);
  ctx_.emplace(std::move(ctx));
  assert(!destructor_.hasValue());
  if (destructor) {
    destructor_ = std::move(destructor);
  }

  typeId_ = static_cast<uint32_t>(Reply::typeId);

  return caretReply_.prepare(
      std::move(reply),
      ctx_->reqid_,
      codecIdRange,
      compressionCodecMap,
      ctx_->getDropProbability(),
      ctx_->getServerLoad(),
      iovsBegin_,
      iovsCount_);
}

} // memcache
} // facebook
