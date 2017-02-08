/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/network/AsciiSerialized.h"
#include "mcrouter/lib/network/CaretSerializedMessage.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {

class WriteBuffer {
 private:
  UniqueIntrusiveListHook hook_;
  using Destructor = std::unique_ptr<void, void (*)(void*)>;
  folly::Optional<Destructor> destructor_;

 public:
  using Queue = UniqueIntrusiveList<WriteBuffer, &WriteBuffer::hook_>;

  explicit WriteBuffer(mc_protocol_t protocol);
  ~WriteBuffer();

  /**
   * Allows using this buffer again without doing a complete
   * re-initialization
   */
  void clear();

  /**
   * If successful, iovOut/niovOut will on return point to an array of iovs
   * contained within this struct which will contain a serialized
   * representation of the given reply.
   *
   * @param destructor  Callback to destruct data used by this reply, called
   *                    when this WriteBuffer is cleared for reuse, or is
   *                    destroyed
   *
   * @return true On success
   */
  template <class Reply>
  typename std::enable_if<
      ListContains<
          McRequestList,
          RequestFromReplyType<Reply, RequestReplyPairs>>::value,
      bool>::type
  prepareTyped(
      McServerRequestContext&& ctx,
      Reply&& reply,
      Destructor destructor,
      const CompressionCodecMap* compressionCodecMap,
      const CodecIdRange& codecIdRange);

  template <class Reply>
  typename std::enable_if<
      !ListContains<
          McRequestList,
          RequestFromReplyType<Reply, RequestReplyPairs>>::value,
      bool>::type
  prepareTyped(
      McServerRequestContext&& ctx,
      Reply&& reply,
      Destructor destructor,
      const CompressionCodecMap* compressionCodecMap,
      const CodecIdRange& codecIdRange);

  const struct iovec* getIovsBegin() const {
    return iovsBegin_;
  }
  size_t getIovsCount() const {
    return iovsCount_;
  }

  /**
   * Checks if we should send a reply for this request.
   *
   * A possible scenario of when request is marked as noreply after being
   * serialized is when one key in multi-op batch had an error.
   *
   * @return false  iff the reply is marked as noreply and we shouldn't send it
   *                over the network.
   */
  bool noReply() const;

  bool isSubRequest() const;
  bool isEndContext() const;

  bool isEndOfBatch() const {
    return isEndOfBatch_;
  }

  void markEndOfBatch() {
    isEndOfBatch_ = true;
  }

  uint32_t typeId() const {
    return typeId_;
  }

 private:
  const mc_protocol_t protocol_;

  /* Write buffers */
  union {
    AsciiSerializedReply asciiReply_;
    UmbrellaSerializedMessage umbrellaReply_;
    CaretSerializedMessage caretReply_;
  };

  folly::Optional<McServerRequestContext> ctx_;
  const struct iovec* iovsBegin_;
  size_t iovsCount_{0};
  bool isEndOfBatch_{false};
  uint32_t typeId_{0};

  WriteBuffer(const WriteBuffer&) = delete;
  WriteBuffer& operator=(const WriteBuffer&) = delete;
  WriteBuffer(WriteBuffer&&) noexcept = delete;
  WriteBuffer& operator=(WriteBuffer&&) = delete;
};

// The only purpose of this class is to avoid a circular #include dependency
// between WriteBuffer.h and McServerSession.h.
class WriteBufferIntrusiveList : public WriteBuffer::Queue {};

class WriteBufferQueue {
 public:
  explicit WriteBufferQueue(mc_protocol_t protocol) noexcept
      : protocol_(protocol), tlFreeQueue_(initFreeQueue(protocol_)) {}

  std::unique_ptr<WriteBuffer> get() {
    if (tlFreeQueue_.empty()) {
      return folly::make_unique<WriteBuffer>(protocol_);
    } else {
      return tlFreeQueue_.popFront();
    }
  }

  void push(std::unique_ptr<WriteBuffer> wb) {
    queue_.pushBack(std::move(wb));
  }

  void pop(bool popBatch) {
    bool done = false;
    do {
      assert(!empty());
      if (tlFreeQueue_.size() < kMaxFreeQueueSz) {
        auto& wb = tlFreeQueue_.pushBack(queue_.popFront());
        done = wb.isEndOfBatch();
        wb.clear();
      } else {
        done = queue_.popFront()->isEndOfBatch();
      }
    } while (!done && popBatch);
  }

  bool empty() const noexcept {
    return queue_.empty();
  }

 private:
  constexpr static size_t kMaxFreeQueueSz = 50;

  mc_protocol_t protocol_;
  WriteBuffer::Queue& tlFreeQueue_;
  WriteBuffer::Queue queue_;

  static WriteBuffer::Queue& initFreeQueue(mc_protocol_t protocol) noexcept;

  WriteBufferQueue(const WriteBufferQueue&) = delete;
  WriteBufferQueue& operator=(const WriteBufferQueue&) = delete;
  WriteBufferQueue(WriteBufferQueue&&) noexcept = delete;
  WriteBufferQueue& operator=(WriteBufferQueue&&) = delete;
};
}
} // facebook::memcache

#include "WriteBuffer-inl.h"
