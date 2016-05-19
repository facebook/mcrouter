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

#include <utility>

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedMsg.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

namespace detail {
  // Hack: we rely on the fact that request typeId's are always odd and the
  // corresponding reply's typeId is the request's typeId + 1.
  inline bool isRequestTypeId(uint32_t typeId) {
    return typeId % 2 == 1;
  }
} // detail

template <class Callback>
void ClientServerMcParser<Callback>::parse(folly::ByteRange data,
                                           uint32_t typeId,
                                           bool isFirstPacket) {
  const auto isRequest = detail::isRequestTypeId(typeId);

  auto source = data.begin();
  size_t size = data.size();
  while (size > 0) {
    std::pair<void*, size_t> buffer;
    if (isRequest) {
      buffer = requestParser_->getReadBuffer();
    } else {
      buffer = replyParser_->getReadBuffer();
    }

    size_t numBytes = std::min(buffer.second, size);
    memcpy(buffer.first, source, numBytes);

    if (isRequest) {
      requestParser_->readDataAvailable(numBytes);
    } else {
      // Inform replyParser_ that a reply with type corresponding to typeId is
      // about to be parsed
      replyParser_->setProtocol(determineProtocol(*data.begin()));
      if (isFirstPacket) {
        expectNextDispatcher_.dispatch(typeId - 1);
      }

      replyParser_->readDataAvailable(numBytes);
    }

    size -= numBytes;
    source += numBytes;
  }
}

}} // facebook::memcache
