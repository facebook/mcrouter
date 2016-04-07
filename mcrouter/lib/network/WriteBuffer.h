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
#include <folly/ThreadLocal.h>

#include "mcrouter/lib/mc/ascii_response.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/CaretSerializedMessage.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
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

  bool prepare(const McReply& reply, mc_op_t operation,
               const folly::Optional<folly::IOBuf>& key,
               struct iovec*& iovOut, size_t& niovOut);

  template <class ThriftType>
  bool prepare(TypedThriftReply<ThriftType>&& reply,
               folly::Optional<folly::IOBuf>& key,
               struct iovec*& iovOut, size_t& niovOut,
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
               struct iovec*& iovOut, size_t& niovOut,
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
               struct iovec*&, size_t&) {
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
  mc_ascii_response_buf_t asciiResponse_; // Only for McReply

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
  // Server and client error helper
  void handleError(mc_res_t result, uint16_t errorCode, std::string&& message);
  void handleUnexpected(mc_res_t result, const char* requestName);
};

class WriteBuffer {
 private:
  UniqueIntrusiveListHook hook_;

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
   * @return true On success
   */
  bool prepare(McServerRequestContext&& ctx, McReply&& reply);

  template <class Reply>
  bool prepareTyped(McServerRequestContext&& ctx, Reply&& reply, size_t typeId);

  struct iovec* getIovsBegin() {
    return iovsBegin_;
  }
  size_t getIovsCount() { return iovsCount_; }

  /**
   * For umbrellaProtocol only
   * Ensure that the WriteBuffer uses the same type of
   * umbrella protocol. If not, reinitialize accordingly
   */
  void ensureType(UmbrellaVersion type);

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

 private:
  const mc_protocol_t protocol_;
  UmbrellaVersion version_{UmbrellaVersion::BASIC};

  /* Write buffers */
  union {
    AsciiSerializedReply asciiReply_;
    UmbrellaSerializedMessage umbrellaReply_;
    CaretSerializedMessage caretReply_;
  };

  folly::Optional<McServerRequestContext> ctx_;
  folly::Optional<McReply> reply_;
  struct iovec* iovsBegin_;
  size_t iovsCount_{0};

  WriteBuffer(const WriteBuffer&) = delete;
  WriteBuffer& operator=(const WriteBuffer&) = delete;
  WriteBuffer(WriteBuffer&&) noexcept = delete;
  WriteBuffer& operator=(WriteBuffer&&) = delete;
};

class WriteBufferQueue {
 public:
  explicit WriteBufferQueue(mc_protocol_t protocol)
      : protocol_(protocol) {
    if (protocol_ != mc_ascii_protocol &&
        protocol_ != mc_umbrella_protocol) {
      throw std::runtime_error("Invalid protocol");
    }
  }

  std::unique_ptr<WriteBuffer> get() {
    auto& freeQ = freeQueue();
    if (freeQ.empty()) {
      return folly::make_unique<WriteBuffer>(protocol_);
    } else {
      return freeQ.popFront();
    }
  }

  void push(std::unique_ptr<WriteBuffer> wb) { queue_.pushBack(std::move(wb)); }

  void pop() {
    auto& freeQ = freeQueue();
    if (freeQ.size() < kMaxFreeQueueSz) {
      auto& wb = freeQ.pushBack(queue_.popFront());
      wb.clear();
    } else {
      queue_.popFront();
    }
  }

  bool empty() {
    return queue_.empty();
  }

 private:
  mc_protocol_t protocol_;
  WriteBuffer::Queue& freeQueue() {
    static folly::ThreadLocal<WriteBuffer::Queue> freeQ[mc_nprotocols];
    assert((size_t)protocol_ < mc_nprotocols);
    return *freeQ[(size_t)protocol_];
  }
  WriteBuffer::Queue queue_;
  constexpr static size_t kMaxFreeQueueSz = 50;

  WriteBufferQueue(const WriteBufferQueue&) = delete;
  WriteBufferQueue& operator=(const WriteBufferQueue&) = delete;
  WriteBufferQueue(WriteBufferQueue&&) noexcept = delete;
  WriteBufferQueue& operator=(WriteBufferQueue&&) = delete;
};

}}  // facebook::memcache

#include "WriteBuffer-inl.h"
