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

#include <string>

#include <folly/io/IOBuf.h>
#include <folly/Optional.h>

#include "mcrouter/lib/mc/ascii_response.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/CaretSerializedMessage.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"

namespace facebook { namespace memcache {

// TODO(jmswen) When we kill McReply and McRequest, it will be easy to merge
// AsciiSerializedReply and AsciiSerializedRequest into one class.
class AsciiSerializedReply {
 public:
  AsciiSerializedReply();

  AsciiSerializedReply(const AsciiSerializedReply&) = delete;
  AsciiSerializedReply& operator=(const AsciiSerializedReply&) = delete;
  AsciiSerializedReply(AsciiSerializedReply&&) noexcept = delete;
  AsciiSerializedReply& operator=(AsciiSerializedReply&&) = delete;

  ~AsciiSerializedReply();

  void clear();

  bool prepare(McReply&& reply, mc_op_t operation,
               const folly::Optional<folly::IOBuf>& key,
               const struct iovec*& iovOut, size_t& niovOut);

  template <class ThriftType>
  bool prepare(TypedThriftReply<ThriftType>&& reply,
               folly::Optional<folly::IOBuf>& key,
               const struct iovec*& iovOut, size_t& niovOut,
               GetLikeT<McOperation<OpFromType<ThriftType,
                                               ReplyOpMapping>::value>> = 0) {
    iovsCount_ = 0;
    iobuf_.clear();
    auxString_.clear();
    if (key.hasValue()) {
      key->coalesce();
    }
    prepareImpl(
        std::move(reply),
        key.hasValue()
          ? folly::StringPiece(reinterpret_cast<const char*>(key->data()),
                               key->length())
          : folly::StringPiece());
    iovOut = iovs_;
    niovOut = iovsCount_;
    return true;
  }

  template <class ThriftType>
  bool prepare(TypedThriftReply<ThriftType>&& reply,
               const folly::Optional<folly::IOBuf>& /* key */,
               const struct iovec*& iovOut, size_t& niovOut,
               OtherThanT<McOperation<OpFromType<ThriftType,
                                      ReplyOpMapping>::value>,
                          GetLike<>> = 0) {
    iovsCount_ = 0;
    iobuf_.clear();
    auxString_.clear();
    prepareImpl(std::move(reply));
    iovOut = iovs_;
    niovOut = iovsCount_;
    return true;
  }

  template <class Unsupported>
  bool prepare(Unsupported&&, const folly::Optional<folly::IOBuf>&,
               const struct iovec*&, size_t&) {
    return false;
  }

 private:
  // See comment in prepareImpl for McMetagetReply for explanation
  static constexpr size_t kMaxBufferLength = 100;

  static const size_t kMaxIovs = 16;
  struct iovec iovs_[kMaxIovs];
  size_t iovsCount_{0};
  char printBuffer_[kMaxBufferLength];
  // Used to keep alive the reply's IOBuf field (value, stats, etc.). For now,
  // replies have at most one IOBuf, so we only need one here. Note that one of
  // the iovs_ will point into the data managed by this IOBuf. A serialized
  // reply should not set iobuf_ more than once.
  // We also keep an auxiliary string for a similar purpose.
  folly::IOBuf iobuf_;
  std::string auxString_;

  // Only for McReply
  folly::Optional<McReply> reply_;
  mc_ascii_response_buf_t asciiResponse_;

  void addString(folly::ByteRange range);
  void addString(folly::StringPiece str);

  template <class Arg1, class Arg2>
  void addStrings(Arg1&& arg1, Arg2&& arg2);
  template <class Arg, class... Args>
  void addStrings(Arg&& arg, Args&&... args);

  // Get-like ops
  void prepareImpl(TypedThriftReply<cpp2::McGetReply>&& reply,
                   folly::StringPiece key);
  void prepareImpl(TypedThriftReply<cpp2::McGetsReply>&& reply,
                   folly::StringPiece key);
  void prepareImpl(TypedThriftReply<cpp2::McMetagetReply>&& reply,
                   folly::StringPiece key);
  void prepareImpl(TypedThriftReply<cpp2::McLeaseGetReply>&& reply,
                   folly::StringPiece key);
  // Update-like ops
  void prepareUpdateLike(mc_res_t result, uint16_t errorCode,
                         std::string&& message, const char* requestName);
  void prepareImpl(TypedThriftReply<cpp2::McSetReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McAddReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McReplaceReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McAppendReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McPrependReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McCasReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McLeaseSetReply>&& reply);
  // Arithmetic-like ops
  void prepareArithmeticLike(mc_res_t result, const uint64_t delta,
                             uint16_t errorCode, std::string&& message,
                             const char* requestName);
  void prepareImpl(TypedThriftReply<cpp2::McIncrReply>&& reply);
  void prepareImpl(TypedThriftReply<cpp2::McDecrReply>&& reply);
  // Delete
  void prepareImpl(TypedThriftReply<cpp2::McDeleteReply>&& reply);
  // Touch
  void prepareImpl(TypedThriftReply<cpp2::McTouchReply>&& reply);
  // Version
  void prepareImpl(const TypedThriftReply<cpp2::McVersionReply>& reply);
  void prepareImpl(TypedThriftReply<cpp2::McVersionReply>&& reply);
  // Miscellaneous
  void prepareImpl(TypedThriftReply<cpp2::McStatsReply>&&);
  void prepareImpl(TypedThriftReply<cpp2::McShutdownReply>&&);
  void prepareImpl(TypedThriftReply<cpp2::McQuitReply>&&) {} // always noreply
  void prepareImpl(TypedThriftReply<cpp2::McExecReply>&&);
  void prepareImpl(TypedThriftReply<cpp2::McFlushReReply>&&);
  void prepareImpl(TypedThriftReply<cpp2::McFlushAllReply>&&);
  // Server and client error helper
  void handleError(mc_res_t result, uint16_t errorCode, std::string&& message);
  void handleUnexpected(mc_res_t result, const char* requestName);
};

class WriteBuffer {
 private:
  UniqueIntrusiveListHook hook_;
  using Destructor = std::unique_ptr<void, void (*)(void*)>;
  folly::Optional<Destructor> destructor_;

 public:
  using Queue = UniqueIntrusiveList<WriteBuffer,
                                    &WriteBuffer::hook_>;

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
  bool prepare(
      McServerRequestContext&& ctx,
      McReply&& reply,
      Destructor destructor = Destructor(nullptr, nullptr));

  template <class Reply>
  bool prepareTyped(
      McServerRequestContext&& ctx,
      Reply&& reply,
      Destructor destructor = Destructor(nullptr, nullptr));

  const struct iovec* getIovsBegin() {
    return iovsBegin_;
  }
  size_t getIovsCount() { return iovsCount_; }

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

  bool isEndOfBatch() const {
    return isEndOfBatch_;
  }

  void markEndOfBatch() {
    isEndOfBatch_ = true;
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

  WriteBuffer(const WriteBuffer&) = delete;
  WriteBuffer& operator=(const WriteBuffer&) = delete;
  WriteBuffer(WriteBuffer&&) noexcept = delete;
  WriteBuffer& operator=(WriteBuffer&&) = delete;
};

// The only purpose of this class is to avoid a circular #include dependency
// between WriteBuffer.h and McServerSession.h.
class WriteBufferIntrusiveList : public WriteBuffer::Queue {
};

class WriteBufferQueue {
 public:
  explicit WriteBufferQueue(mc_protocol_t protocol) noexcept
      : protocol_(protocol),
        tlFreeQueue_(initFreeQueue(protocol_)) {
  }

  std::unique_ptr<WriteBuffer> get() {
    if (tlFreeQueue_.empty()) {
      return folly::make_unique<WriteBuffer>(protocol_);
    } else {
      return tlFreeQueue_.popFront();
    }
  }

  void push(std::unique_ptr<WriteBuffer> wb) { queue_.pushBack(std::move(wb)); }

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

  static WriteBuffer::Queue& initFreeQueue(mc_protocol_t protocol) noexcept {
    assert(protocol == mc_ascii_protocol ||
           protocol == mc_umbrella_protocol ||
           protocol == mc_caret_protocol);

    static thread_local WriteBuffer::Queue freeQ[mc_nprotocols];
    return freeQ[static_cast<size_t>(protocol)];
  }

  WriteBufferQueue(const WriteBufferQueue&) = delete;
  WriteBufferQueue& operator=(const WriteBufferQueue&) = delete;
  WriteBufferQueue(WriteBufferQueue&&) noexcept = delete;
  WriteBufferQueue& operator=(WriteBufferQueue&&) = delete;
};

}}  // facebook::memcache

#include "WriteBuffer-inl.h"
