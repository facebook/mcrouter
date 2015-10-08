/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McParser.h"

#include <folly/Bits.h>
#include <folly/Memory.h>

#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

/* Adjust buffer size after this many requests */
const size_t kAdjustBufferSizeInterval = 10000;

/* Decay previous bytes per request value with this constant */
const double kBprDecay = 0.9;

McParser::McParser(ParserCallback& callback,
                   size_t requestsPerRead,
                   size_t minBufferSize,
                   size_t maxBufferSize)
    : callback_(callback),
      messagesPerRead_(requestsPerRead),
      minBufferSize_(minBufferSize),
      maxBufferSize_(maxBufferSize),
      bufferSize_(maxBufferSize),
      readBuffer_(folly::IOBuf::CREATE, bufferSize_) {
}

McParser::~McParser() {
}

void McParser::shrinkBuffers() {
  if (readBuffer_.length() == 0 && bufferShrinkRequired_) {
    readBuffer_ = folly::IOBuf(folly::IOBuf::CREATE, bufferSize_);
    bufferShrinkRequired_ = false;
  }
}

void McParser::reset() {
  readBuffer_.clear();
  umBodyBuffer_.reset();
}

std::pair<void*, size_t> McParser::getReadBuffer() {
  if (protocol_ == mc_umbrella_protocol
      && umBodyBuffer_) {
    /* We're reading in umbrella message body */
    return std::make_pair(umBodyBuffer_->writableTail(),
                          umMsgInfo_.bodySize - umBodyBuffer_->length());
  } else {
    readBuffer_.unshare();
    if (!readBuffer_.length() && readBuffer_.capacity() > 0) {
      /* If we read everything, reset pointers to 0 and re-use the buffer */
      readBuffer_.clear();
    } else if (readBuffer_.headroom() > 0) {
      /* Move partially read data to the beginning */
      readBuffer_.retreat(readBuffer_.headroom());
    } else {
      /* Reallocate more space if necessary */
      bufferShrinkRequired_ = true;
      readBuffer_.reserve(0, bufferSize_);
    }
    return std::make_pair(readBuffer_.writableTail(),
                          std::min(readBuffer_.tailroom(), bufferSize_));
  }
}

void McParser::recalculateBufferSize(size_t read) {
  readBytes_ += read;
  if (LIKELY(parsedMessages_ < kAdjustBufferSizeInterval)) {
    return;
  }

  double bpr = (double)readBytes_ / parsedMessages_;
  if (UNLIKELY(bytesPerRequest_ == 0.0)) {
    bytesPerRequest_ = bpr;
  } else {
    bytesPerRequest_ = bytesPerRequest_ * kBprDecay + bpr * (1.0 - kBprDecay);
  }
  bufferSize_ = std::max(
    minBufferSize_,
    std::min((size_t)bytesPerRequest_ * messagesPerRead_, maxBufferSize_));
  parsedMessages_ = 0;
  readBytes_ = 0;
}

bool McParser::readUmbrellaData() {
  while (!readBuffer_.empty()) {
    auto st = umbrellaParseHeader(readBuffer_.data(),
                                  readBuffer_.length(),
                                  umMsgInfo_);
    if (st == UmbrellaParseStatus::NOT_ENOUGH_DATA) {
      return true;
    }

    if (st != UmbrellaParseStatus::OK) {
      callback_.parseError(mc_res_remote_error,
                           "Error parsing Umbrella header");
      return false;
    }

    /* Three cases: */
    auto messageSize = umMsgInfo_.headerSize + umMsgInfo_.bodySize;
    if (readBuffer_.length() >= messageSize) {
      /* 1) we already have the entire message */
      if (!callback_.umMessageReady(
            umMsgInfo_,
            readBuffer_.data(),
            readBuffer_.data() + umMsgInfo_.headerSize,
            readBuffer_)) {
        readBuffer_.clear();
        return false;
      }
      /* Re-enter the loop */
      readBuffer_.trimStart(messageSize);
      continue;
    } else if (readBuffer_.length() >= umMsgInfo_.headerSize &&
               messageSize - readBuffer_.length() >
               minBufferSize_) {
      /* 2) we have the entire header, but body is incomplete.
         Copy the partially read body into the new buffer.
         TODO: this copy could be eliminated, but needs
         some modification of umbrella library. */
      auto partial = readBuffer_.length() - umMsgInfo_.headerSize;
      umBodyBuffer_ = folly::IOBuf::copyBuffer(
        readBuffer_.data() + umMsgInfo_.headerSize,
        partial,
        /* headroom= */ 0,
        /* minTailroom= */ umMsgInfo_.bodySize - partial);
      return true;
    }
    /* 3) else header is incomplete */
    return true;
  }
  return true;
}

bool McParser::readDataAvailable(size_t len) {
  SCOPE_EXIT {
    if (messagesPerRead_ > 0) {
      recalculateBufferSize(len);
    }
  };

  if (umBodyBuffer_) {
    umBodyBuffer_->append(len);
    if (umBodyBuffer_->length() == umMsgInfo_.bodySize) {
      auto res = callback_.umMessageReady(umMsgInfo_,
                                          readBuffer_.data(),
                                          umBodyBuffer_->data(),
                                          *umBodyBuffer_);
      readBuffer_.clear();
      umBodyBuffer_.reset();
      return res;
    }
    return true;
  } else {
    readBuffer_.append(len);
    if (UNLIKELY(readBuffer_.empty())) {
      return true;
    }

    if (UNLIKELY(!seenFirstByte_)) {
      seenFirstByte_ = true;
      protocol_ = determineProtocol(*readBuffer_.data());
      if (protocol_ == mc_umbrella_protocol) {
        outOfOrder_ = true;
      } else if (protocol_ == mc_ascii_protocol) {
        outOfOrder_ = false;
      } else {
        return false;
      }
    }

    if (protocol_ == mc_umbrella_protocol) {
      const bool ret = readUmbrellaData();
      shrinkBuffers(); /* no-op if buffer is not large */
      return ret;
    } else {
      callback_.handleAscii(readBuffer_);
      return true;
    }
  }
}

}}  // facebook::memcache
