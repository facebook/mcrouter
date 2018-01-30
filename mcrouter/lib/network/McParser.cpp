/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McParser.h"

#include <algorithm>
#include <new>
#include <utility>

#include <folly/Format.h>
#include <folly/ThreadLocal.h>
#include <folly/experimental/JemallocNodumpAllocator.h>
#include <folly/io/Cursor.h>
#include <folly/lang/Bits.h>

#include "mcrouter/lib/Clocks.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

namespace {
// Adjust buffer size after this many CPU cycles (~2 billion)
constexpr uint64_t kAdjustBufferSizeCpuCycles = 1UL << 31;

size_t mcOpToRequestTypeId(mc_op_t mc_op) {
  switch (mc_op) {
#define THRIFT_OP(MC_OPERATION)                                           \
  case MC_OPERATION::mc_op: {                                             \
    using Request =                                                       \
        typename TypeFromOp<MC_OPERATION::mc_op, RequestOpMapping>::type; \
    return Request::typeId;                                               \
  }
#include "mcrouter/lib/McOpList.h"

    default:
      return 0;
  }
}

#ifdef FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED

folly::ThreadLocal<folly::JemallocNodumpAllocator> allocator;

folly::IOBuf copyToNodumpBuffer(
    const UmbrellaMessageInfo& umMsgInfo,
    const folly::IOBuf& readBuffer) {
  // Allocate buffer
  const size_t bufSize = umMsgInfo.headerSize + umMsgInfo.bodySize;
  void* p = allocator->allocate(bufSize);
  if (!p) {
    LOG(WARNING) << "Not enough memory to create a nodump buffer";
    throw std::bad_alloc();
  }
  // Copy data
  folly::io::Cursor c(&readBuffer);
  c.pull(p, readBuffer.length());
  // Transfer ownership to a new IOBuf
  return folly::IOBuf(
      folly::IOBuf::TAKE_OWNERSHIP,
      p,
      bufSize,
      readBuffer.length(),
      folly::JemallocNodumpAllocator::deallocate,
      reinterpret_cast<void*>(allocator->getFlags()));
}

#endif

} // anonymous

McParser::McParser(
    ParserCallback& callback,
    size_t minBufferSize,
    size_t maxBufferSize,
    const bool useJemallocNodumpAllocator,
    ConnectionFifo* debugFifo)
    : callback_(callback),
      bufferSize_(minBufferSize),
      maxBufferSize_(maxBufferSize),
      debugFifo_(debugFifo),
      readBuffer_(folly::IOBuf::CREATE, bufferSize_),
      useJemallocNodumpAllocator_(useJemallocNodumpAllocator) {
#ifndef FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
  useJemallocNodumpAllocator_ = false;
#endif
}

void McParser::reset() {
  readBuffer_.clear();
}

std::pair<void*, size_t> McParser::getReadBuffer() {
  assert(!readBuffer_.isChained());
  readBuffer_.unshareOne();
  if (!readBuffer_.length()) {
    assert(readBuffer_.capacity() > 0);
    /* If we read everything, reset pointers to 0 and re-use the buffer */
    readBuffer_.clear();
  } else if (readBuffer_.headroom() > 0) {
    /* Move partially read data to the beginning */
    readBuffer_.retreat(readBuffer_.headroom());
  } else {
    /* Reallocate more space if necessary */
    readBuffer_.reserve(0, bufferSize_);
  }
  return std::make_pair(readBuffer_.writableTail(), readBuffer_.tailroom());
}

bool McParser::readUmbrellaOrCaretData() {
  while (readBuffer_.length() > 0) {
    // Parse header
    UmbrellaParseStatus parseStatus;
    if (protocol_ == mc_umbrella_protocol_DONOTUSE) {
      parseStatus = umbrellaParseHeader(
          readBuffer_.data(), readBuffer_.length(), umMsgInfo_);
    } else {
      parseStatus = caretParseHeader(
          readBuffer_.data(), readBuffer_.length(), umMsgInfo_);
    }

    if (parseStatus == UmbrellaParseStatus::NOT_ENOUGH_DATA) {
      return true;
    }

    if (parseStatus != UmbrellaParseStatus::OK) {
      callback_.parseError(
          mc_res_remote_error,
          folly::sformat(
              "Error parsing {} header", mc_protocol_to_string(protocol_)));
      return false;
    }

    const auto messageSize = umMsgInfo_.headerSize + umMsgInfo_.bodySize;

    // Parse message body
    // Case 1: Entire message (and possibly part of next) is in the buffer
    if (readBuffer_.length() >= messageSize) {
      if (UNLIKELY(debugFifo_ && debugFifo_->isConnected())) {
        if (protocol_ == mc_umbrella_protocol_DONOTUSE) {
          const auto mc_op = umbrellaDetermineOperation(
              readBuffer_.data(), umMsgInfo_.headerSize);
          umMsgInfo_.typeId = mcOpToRequestTypeId(mc_op);
          if (umMsgInfo_.typeId != 0 &&
              umbrellaIsReply(readBuffer_.data(), umMsgInfo_.headerSize)) {
            // We assume reply typeId is always one plus corresponding
            // request's typeId. We rely on this in ClientServerMcParser.h.
            ++umMsgInfo_.typeId;
          }
        }
        debugFifo_->startMessage(MessageDirection::Received, umMsgInfo_.typeId);
        debugFifo_->writeData(readBuffer_.writableData(), messageSize);
      }

      bool cbStatus;
      if (protocol_ == mc_umbrella_protocol_DONOTUSE) {
        cbStatus = callback_.umMessageReady(umMsgInfo_, readBuffer_);
      } else {
        cbStatus = callback_.caretMessageReady(umMsgInfo_, readBuffer_);
      }

      if (!cbStatus) {
        readBuffer_.clear();
        return false;
      }
      readBuffer_.trimStart(messageSize);
      continue;
    }

    // Case 2: We don't have full header, so return to wait for more data
    if (readBuffer_.length() < umMsgInfo_.headerSize) {
      return true;
    }

    // Case 3: We have the full header, but not the full body. If needed,
    // reallocate into a buffer large enough for full header and body. Then
    // return to wait for remaining data.
    if (readBuffer_.length() + readBuffer_.tailroom() < messageSize) {
      assert(!readBuffer_.isChained());
      readBuffer_.unshareOne();
      bufferSize_ = std::max<size_t>(bufferSize_, messageSize);
      readBuffer_.reserve(
          0 /* minHeadroom */,
          bufferSize_ - readBuffer_.length() /* minTailroom */);
    }
#ifdef FOLLY_JEMALLOC_NODUMP_ALLOCATOR_SUPPORTED
    if (useJemallocNodumpAllocator_) {
      readBuffer_ = copyToNodumpBuffer(umMsgInfo_, readBuffer_);
    }
#endif
    return true;
  }

  // We parsed everything, read buffer is empty.
  // Try to shrink it to reduce memory footprint
  if (bufferSize_ > maxBufferSize_) {
    auto curCycles = cycles::getCpuCycles();
    if (curCycles > lastShrinkCycles_ + kAdjustBufferSizeCpuCycles) {
      lastShrinkCycles_ = curCycles;
      bufferSize_ = maxBufferSize_;
      readBuffer_ = folly::IOBuf(folly::IOBuf::CREATE, bufferSize_);
    }
  }
  return true;
}

bool McParser::readDataAvailable(size_t len) {
  // Caller is responsible for ensuring the read buffer has enough tailroom
  readBuffer_.append(len);
  if (UNLIKELY(readBuffer_.length() == 0)) {
    return true;
  }

  if (UNLIKELY(!seenFirstByte_)) {
    seenFirstByte_ = true;
    protocol_ = determineProtocol(*readBuffer_.data());
    if (protocol_ == mc_ascii_protocol) {
      outOfOrder_ = false;
    } else {
      assert(
          protocol_ == mc_umbrella_protocol_DONOTUSE ||
          protocol_ == mc_caret_protocol);
      outOfOrder_ = true;
    }
  }

  if (protocol_ == mc_ascii_protocol) {
    callback_.handleAscii(readBuffer_);
    return true;
  }
  return readUmbrellaOrCaretData();
}

double McParser::getDropProbability() const {
  return static_cast<double>(umMsgInfo_.dropProbability) /
      kDropProbabilityNormalizer;
}

} // memcache
} // facebook
