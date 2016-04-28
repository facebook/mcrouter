/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <folly/String.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/mc/ascii_response.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook { namespace memcache {

WriteBuffer::WriteBuffer(mc_protocol_t protocol)
    : protocol_(protocol) {
  switch (protocol_) {
    case mc_ascii_protocol:
      new (&asciiReply_) AsciiSerializedReply;
      break;

    case mc_umbrella_protocol:
      new (&umbrellaReply_) UmbrellaSerializedMessage;
      break;

    case mc_caret_protocol:
      new (&caretReply_) CaretSerializedMessage;
      break;

    default:
      CHECK(false) << "Unknown protocol";
  }
}

WriteBuffer::~WriteBuffer() {
  switch (protocol_) {
    case mc_ascii_protocol:
      asciiReply_.~AsciiSerializedReply();
      break;

    case mc_umbrella_protocol:
      umbrellaReply_.~UmbrellaSerializedMessage();
      break;

    case mc_caret_protocol:
      caretReply_.~CaretSerializedMessage();
      break;

    default:
      CHECK(false);
  }
}

void WriteBuffer::clear() {
  ctx_.clear();
  destructor_.clear();
  isEndOfBatch_ = false;

  switch (protocol_) {
    case mc_ascii_protocol:
      asciiReply_.clear();
      break;

    case mc_umbrella_protocol:
      umbrellaReply_.clear();
      break;

    case mc_caret_protocol:
      caretReply_.clear();
      break;

    default:
      CHECK(false);
  }
}

bool WriteBuffer::prepare(
    McServerRequestContext&& ctx,
    McReply&& reply,
    Destructor destructor) {
  ctx_.emplace(std::move(ctx));
  assert(!destructor_.hasValue());
  if (destructor) {
    destructor_ = std::move(destructor);
  }

  switch (protocol_) {
    case mc_ascii_protocol:
      return asciiReply_.prepare(std::move(reply),
                                 ctx_->operation_,
                                 ctx_->asciiKey(),
                                 iovsBegin_,
                                 iovsCount_);

    case mc_umbrella_protocol:
      return umbrellaReply_.prepare(std::move(reply),
                                    ctx_->operation_,
                                    ctx_->reqid_,
                                    iovsBegin_,
                                    iovsCount_);

    default:
      // mc_caret_protocol not supported with old McRequest/McReply
      CHECK(false);
  }
}

bool WriteBuffer::noReply() const {
  return ctx_.hasValue() && ctx_->hasParent() && ctx_->parent().error();
}

AsciiSerializedReply::AsciiSerializedReply() {
  mc_ascii_response_buf_init(&asciiResponse_);
}

AsciiSerializedReply::~AsciiSerializedReply() {
  mc_ascii_response_buf_cleanup(&asciiResponse_);
}

void AsciiSerializedReply::clear() {
  mc_ascii_response_buf_cleanup(&asciiResponse_);
  mc_ascii_response_buf_init(&asciiResponse_);
  reply_.clear();
}

bool AsciiSerializedReply::prepare(McReply&& reply,
                                   mc_op_t operation,
                                   const folly::Optional<folly::IOBuf>& key,
                                   const struct iovec*& iovOut,
                                   size_t& niovOut) {
  reply_.emplace(std::move(reply));

  mc_msg_t replyMsg;
  mc_msg_init_not_refcounted(&replyMsg);
  reply_->dependentMsg(operation, &replyMsg);

  nstring_t k;
  if (key.hasValue()) {
    k.str = (char*)key->data();
    k.len = key->length();
  } else {
    k.str = nullptr;
    k.len = 0;
  }
  niovOut = mc_ascii_response_write_iovs(
    &asciiResponse_,
    k,
    operation,
    &replyMsg,
    iovs_,
    kMaxIovs);
  iovOut = iovs_;
  return niovOut != 0;
}

void AsciiSerializedReply::addString(folly::ByteRange range) {
  assert(iovsCount_ < kMaxIovs);
  iovs_[iovsCount_].iov_base = const_cast<unsigned char*>(range.begin());
  iovs_[iovsCount_].iov_len = range.size();
  ++iovsCount_;
}

void AsciiSerializedReply::addString(folly::StringPiece str) {
  // cause implicit conversion.
  addString(folly::ByteRange(str));
}

void AsciiSerializedReply::handleError(mc_res_t result,
                                       uint16_t errorCode,
                                       std::string&& message) {
  assert(isErrorResult(result));

  if (!message.empty()) {
    if (result == mc_res_client_error) {
      addString("CLIENT_ERROR ");
    } else {
      addString("SERVER_ERROR ");
    }
    if (errorCode != 0) {
      const auto len = snprintf(printBuffer_, kMaxBufferLength,
                                "%d ", errorCode);
      assert(len > 0);
      assert(static_cast<size_t>(len) < kMaxBufferLength);
      addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    }
    auxString_ = std::move(message);
    addStrings(auxString_, "\r\n");
  } else {
    addString(mc_res_to_response_string(result));
  }
}

void AsciiSerializedReply::handleUnexpected(mc_res_t result,
                                            const char* requestName) {
  assert(iovsCount_ == 0);

  // Note: this is not totally compatible with the old way of handling
  // unexpected behavior in mc_ascii_response_write_iovs()
  const auto len = snprintf(printBuffer_, kMaxBufferLength,
                           "SERVER_ERROR unexpected result %s (%d) for %s\r\n",
                           mc_res_to_string(result), result, requestName);
  assert(len > 0);
  assert(static_cast<size_t>(len) < kMaxBufferLength);
  addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Get-like ops
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McGetReply>&& reply,
    folly::StringPiece key) {

  if (reply.isHit()) {
    if (key.empty()) {
      // Multi-op hack: if key is empty, this is the END context
      if (reply.isError()) {
        handleError(reply.result(), reply.appSpecificErrorCode(),
                    std::move(reply->message));
      }
      addString("END\r\n");
    } else {
      const auto valueStr = reply.valueRangeSlow();

      const auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %zu\r\n",
                                reply.flags(), valueStr.size());
      assert(len > 0);
      assert(static_cast<size_t>(len) < kMaxBufferLength);

      addStrings("VALUE ", key,
                 folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
      assert(iobuf_.empty());
      // value was coalesced in valueRangeSlow()
      iobuf_ = std::move(reply->value);
      addStrings(valueStr, "\r\n");
    }
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "get");
  }
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McGetsReply>&& reply,
    folly::StringPiece key) {

  if (reply.isHit()) {
    const auto valueStr = reply.valueRangeSlow();
    const uint64_t casToken = reply->get_casToken() ? *reply->get_casToken()
                                                    : 0;

    const auto len = snprintf(printBuffer_, kMaxBufferLength,
                              " %lu %zu %lu\r\n",
                              reply.flags(), valueStr.size(), casToken);
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);

    addStrings("VALUE ", key,
               folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    assert(iobuf_.empty());
    // value was coalesced in valueRangeSlow()
    iobuf_ = std::move(reply->value);
    addStrings(valueStr, "\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "gets");
  }
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McMetagetReply>&& reply,
    folly::StringPiece key) {
  /**
   * META key age: (unknown|\d+); exptime: \d+;
   * from: (\d+\.\d+\.\d+\.\d+|unknown); is_transient: (1|0)\r\n
   *
   * age is at most 11 characters, with - sign.
   * exptime is at most 10 characters.
   * IP6 address is at most 39 characters.
   * To be safe, we set kMaxBufferLength = 100 bytes.
   */
  if (reply.result() == mc_res_found) {
    // age
    std::string ageStr("unknown");
    if (reply->get_age()) {
      ageStr = folly::to<std::string>(*reply->get_age());
    }
    // exptime
    const auto exptimeStr = folly::to<std::string>(
        reply->get_exptime() ? *reply->get_exptime() : 0);
    // from
    std::string fromStr("unknown");
    if (reply->get_ipAddress()) { // assume valid IP
      fromStr = *reply->get_ipAddress();
    }

    const auto len = snprintf(printBuffer_, kMaxBufferLength,
                              "%s; exptime: %s; from: %s",
                              ageStr.data(), exptimeStr.data(), fromStr.data());
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);

    addStrings("META ", key, " age: ",
               folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
               "; is_transient: 0\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "metaget");
  }
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McLeaseGetReply>&& reply,
    folly::StringPiece key) {

  const auto valueStr = reply.valueRangeSlow();

  if (reply.result() == mc_res_found) {
    const auto len = snprintf(printBuffer_, kMaxBufferLength,
                              " %lu %zu\r\n",
                              reply.flags(), valueStr.size());
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);

    addStrings("VALUE ", key,
               folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    assert(iobuf_.empty());
    // value was coalesced in valueRangeSlow()
    iobuf_ = std::move(reply->value);
    addStrings(valueStr, "\r\n");
  } else if (reply.result() == mc_res_notfound) {
    const uint64_t leaseToken =
      reply->get_leaseToken() ? *reply->get_leaseToken() : 0;

    const auto len = snprintf(printBuffer_, kMaxBufferLength,
                              " %lu %lu %zu\r\n",
                              leaseToken, reply.flags(), valueStr.size());
    addStrings("LVALUE ", key,
               folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    iobuf_ = std::move(reply->value);
    addStrings(valueStr, "\r\n");
  } else if (reply.result() == mc_res_notfoundhot) {
    addString("NOT_FOUND_HOT\r\n");
  } else if (reply.isError()) {
    LOG(ERROR) << "Got reply result " << reply.result();
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    LOG(ERROR) << "Got unexpected reply result " << reply.result();
    handleUnexpected(reply.result(), "lease-get");
  }
}

// Update-like ops
void AsciiSerializedReply::prepareUpdateLike(mc_res_t result,
                                             uint16_t errorCode,
                                             std::string&& message,
                                             const char* requestName) {
  if (isErrorResult(result)) {
    handleError(result, errorCode, std::move(message));
    return;
  }

  if (UNLIKELY(result == mc_res_ok)) {
    addString(mc_res_to_response_string(mc_res_stored));
    return;
  }

  switch (result) {
    case mc_res_stored:
    case mc_res_stalestored:
    case mc_res_found:
    case mc_res_notstored:
    case mc_res_notfound:
    case mc_res_exists:
      addString(mc_res_to_response_string(result));
      break;

    default:
      handleUnexpected(result, requestName);
      break;
  }
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McSetReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "set");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McAddReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "add");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McReplaceReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "replace");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McAppendReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "append");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McPrependReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "prepend");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McCasReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "cas");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McLeaseSetReply>&& reply) {
  prepareUpdateLike(
      reply.result(), reply.appSpecificErrorCode(), std::move(reply->message),
      "lease-set");
}

void AsciiSerializedReply::prepareArithmeticLike(mc_res_t result,
                                                 const uint64_t delta,
                                                 uint16_t errorCode,
                                                 std::string&& message,
                                                 const char* requestName) {
  if (isStoredResult(result)) {
    const auto len = snprintf(printBuffer_, kMaxBufferLength, "%lu\r\n",
                              delta);
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  } else if (isMissResult(result)) {
    addString("NOT_FOUND\r\n");
  } else if (isErrorResult(result)) {
    handleError(result, errorCode, std::move(message));
  } else {
    handleUnexpected(result, requestName);
  }
}

// Arithmetic-like ops
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McIncrReply>&& reply) {
  const auto delta = reply->get_delta() ? *reply->get_delta() : 0;
  prepareArithmeticLike(
      reply.result(), delta, reply.appSpecificErrorCode(),
      std::move(reply->message), "incr");
}

void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McDecrReply>&& reply) {
  const auto delta = reply->get_delta() ? *reply->get_delta() : 0;
  prepareArithmeticLike(
      reply.result(), delta, reply.appSpecificErrorCode(),
      std::move(reply->message), "decr");
}

// Delete
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McDeleteReply>&& reply) {
  if (reply.result() == mc_res_deleted) {
    addString("DELETED\r\n");
  } else if (reply.result() == mc_res_notfound) {
    addString("NOT_FOUND\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "delete");
  }
}

// Touch
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McTouchReply>&& reply) {
  if (reply.result() == mc_res_touched) {
    addString("TOUCHED\r\n");
  } else if (reply.result() == mc_res_notfound) {
    addString("NOT_FOUND\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "touch");
  }
}

// Version
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McVersionReply>&& reply) {
  if (reply.result() == mc_res_ok) {
    // TODO(jmswen) Do something sane when version is empty
    addString("VERSION ");
    if (reply->get_value()) {
      const auto valueStr = reply.valueRangeSlow();
      assert(iobuf_.empty());
      // value was coalesced in valueRangeSlow()
      iobuf_ = std::move(reply->value);
      addString(valueStr);
    }
    addString("\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "version");
  }
}

// Stats
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McStatsReply>&& reply) {
  if (reply.result() == mc_res_ok) {
    if (reply->get_stats()) {
      auxString_ = folly::join("\r\n", *reply->get_stats());
      addStrings(auxString_, "\r\n");
    }
    addString("END\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "stats");
  }
}

// FlushAll
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McFlushAllReply>&& reply) {
  if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else { // Don't handleUnexpected(), just return OK
    addString("OK\r\n");
  }
}

// FlushRe
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McFlushReReply>&& reply) {
  if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else { // Don't handleUnexpected(), just return OK
    addString("OK\r\n");
  }
}

// Exec
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McExecReply>&& reply) {
  if (reply.result() == mc_res_ok) {
    if (reply->get_response()) {
      auxString_ = std::move(reply->response);
      addStrings(auxString_, "\r\n");
    } else {
      addString("OK\r\n");
    }
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "exec");
  }
}

// Shutdown
void AsciiSerializedReply::prepareImpl(
    TypedThriftReply<cpp2::McShutdownReply>&& reply) {
  if (reply.result() == mc_res_ok) {
    addString("OK\r\n");
  } else if (reply.isError()) {
    handleError(reply.result(), reply.appSpecificErrorCode(),
                std::move(reply->message));
  } else {
    handleUnexpected(reply.result(), "shutdown");
  }
}

}}  // facebook::memcache
