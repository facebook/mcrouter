/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsciiSerialized.h"

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McResUtil.h"

namespace facebook {
namespace memcache {

void AsciiSerializedRequest::addString(folly::ByteRange range) {
  assert(iovsCount_ < kMaxIovs);
  iovs_[iovsCount_].iov_base = const_cast<unsigned char*>(range.begin());
  iovs_[iovsCount_].iov_len = range.size();
  ++iovsCount_;
}

void AsciiSerializedRequest::addString(folly::StringPiece str) {
  // cause implicit conversion.
  addString(folly::ByteRange(str));
}

template <class Request>
void AsciiSerializedRequest::keyValueRequestCommon(
    folly::StringPiece prefix,
    const Request& request) {
  auto value = coalesceAndGetRange(const_cast<folly::IOBuf&>(request.value()));
  auto len = snprintf(
      printBuffer_,
      kMaxBufferLength,
      " %lu %d %zd\r\n",
      request.flags(),
      request.exptime(),
      value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(
      prefix,
      request.key().fullKey(),
      folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
      value,
      "\r\n");
}

// Get-like ops.
void AsciiSerializedRequest::prepareImpl(const McGetRequest& request) {
  addStrings("get ", request.key().fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McGetsRequest& request) {
  addStrings("gets ", request.key().fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McMetagetRequest& request) {
  addStrings("metaget ", request.key().fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McLeaseGetRequest& request) {
  addStrings("lease-get ", request.key().fullKey(), "\r\n");
}

// Update-like ops.
void AsciiSerializedRequest::prepareImpl(const McSetRequest& request) {
  keyValueRequestCommon("set ", request);
}

void AsciiSerializedRequest::prepareImpl(const McAddRequest& request) {
  keyValueRequestCommon("add ", request);
}

void AsciiSerializedRequest::prepareImpl(const McReplaceRequest& request) {
  keyValueRequestCommon("replace ", request);
}

void AsciiSerializedRequest::prepareImpl(const McAppendRequest& request) {
  keyValueRequestCommon("append ", request);
}

void AsciiSerializedRequest::prepareImpl(const McPrependRequest& request) {
  keyValueRequestCommon("prepend ", request);
}

void AsciiSerializedRequest::prepareImpl(const McCasRequest& request) {
  auto value = coalesceAndGetRange(const_cast<folly::IOBuf&>(request.value()));
  auto len = snprintf(
      printBuffer_,
      kMaxBufferLength,
      " %lu %d %zd %lu\r\n",
      request.flags(),
      request.exptime(),
      value.size(),
      request.casToken());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(
      "cas ",
      request.key().fullKey(),
      folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
      value,
      "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McLeaseSetRequest& request) {
  auto value = coalesceAndGetRange(const_cast<folly::IOBuf&>(request.value()));
  auto len = snprintf(
      printBuffer_,
      kMaxBufferLength,
      " %lu %lu %d %zd\r\n",
      request.leaseToken(),
      request.flags(),
      request.exptime(),
      value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(
      "lease-set ",
      request.key().fullKey(),
      folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
      value,
      "\r\n");
}

// Arithmetic ops.
void AsciiSerializedRequest::prepareImpl(const McIncrRequest& request) {
  auto len =
      snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n", request.delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(
      "incr ",
      request.key().fullKey(),
      folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(const McDecrRequest& request) {
  auto len =
      snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n", request.delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(
      "decr ",
      request.key().fullKey(),
      folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Delete op.
void AsciiSerializedRequest::prepareImpl(const McDeleteRequest& request) {
  addStrings("delete ", request.key().fullKey());
  if (request.exptime() != 0) {
    auto len =
        snprintf(printBuffer_, kMaxBufferLength, " %d\r\n", request.exptime());
    assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  } else {
    addString("\r\n");
  }
}

// Touch op.
void AsciiSerializedRequest::prepareImpl(const McTouchRequest& request) {
  auto len =
      snprintf(printBuffer_, kMaxBufferLength, " %u\r\n", request.exptime());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(
      "touch ",
      request.key().fullKey(),
      folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Version op.
void AsciiSerializedRequest::prepareImpl(const McVersionRequest&) {
  addString("version\r\n");
}

// FlushAll op.

void AsciiSerializedRequest::prepareImpl(const McFlushAllRequest& request) {
  addString("flush_all");
  if (request.delay() != 0) {
    auto len = snprintf(printBuffer_, kMaxBufferLength, " %u", request.delay());
    assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  }
  addString("\r\n");
}

void AsciiSerializedReply::clear() {
  iovsCount_ = 0;
  iobuf_.clear();
  auxString_.clear();
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

void AsciiSerializedReply::handleError(
    mc_res_t result,
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
      const auto len =
          snprintf(printBuffer_, kMaxBufferLength, "%d ", errorCode);
      assert(len > 0);
      assert(static_cast<size_t>(len) < kMaxBufferLength);
      addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    }
    auxString_ = std::move(message);
    addStrings(*auxString_, "\r\n");
  } else {
    addString(mc_res_to_response_string(result));
  }
}

void AsciiSerializedReply::handleUnexpected(
    mc_res_t result,
    const char* requestName) {
  assert(iovsCount_ == 0);

  // Note: this is not totally compatible with the old way of handling
  // unexpected behavior in mc_ascii_response_write_iovs()
  const auto len = snprintf(
      printBuffer_,
      kMaxBufferLength,
      "SERVER_ERROR unexpected result %s (%d) for %s\r\n",
      mc_res_to_string(result),
      result,
      requestName);
  assert(len > 0);
  assert(static_cast<size_t>(len) < kMaxBufferLength);
  addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Get-like ops
void AsciiSerializedReply::prepareImpl(
    McGetReply&& reply,
    folly::StringPiece key) {
  if (isHitResult(reply.result())) {
    if (key.empty()) {
      // Multi-op hack: if key is empty, this is the END context
      if (isErrorResult(reply.result())) {
        handleError(
            reply.result(),
            reply.appSpecificErrorCode(),
            std::move(reply.message()));
      }
      addString("END\r\n");
    } else {
      const auto valueStr = coalesceAndGetRange(reply.value());

      const auto len = snprintf(
          printBuffer_,
          kMaxBufferLength,
          " %lu %zu\r\n",
          reply.flags(),
          valueStr.size());
      assert(len > 0);
      assert(static_cast<size_t>(len) < kMaxBufferLength);

      addStrings(
          "VALUE ",
          key,
          folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
      assert(!iobuf_.hasValue());
      // value was coalesced in coalesceAndGetRange()
      iobuf_ = std::move(reply.value());
      addStrings(valueStr, "\r\n");
    }
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "get");
  }
}

void AsciiSerializedReply::prepareImpl(
    McGetsReply&& reply,
    folly::StringPiece key) {
  if (isHitResult(reply.result())) {
    const auto valueStr = coalesceAndGetRange(reply.value());
    const auto len = snprintf(
        printBuffer_,
        kMaxBufferLength,
        " %lu %zu %lu\r\n",
        reply.flags(),
        valueStr.size(),
        reply.casToken());
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);

    addStrings(
        "VALUE ",
        key,
        folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    assert(!iobuf_.hasValue());
    // value was coalesced in coalescedAndGetRange()
    iobuf_ = std::move(reply.value());
    addStrings(valueStr, "\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "gets");
  }
}

void AsciiSerializedReply::prepareImpl(
    McMetagetReply&& reply,
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
    if (reply.age() != -1) {
      ageStr = folly::to<std::string>(reply.age());
    }
    // exptime
    const auto exptimeStr = folly::to<std::string>(reply.exptime());
    // from
    std::string fromStr("unknown");
    if (!reply.ipAddress().empty()) { // assume valid IP
      fromStr = reply.ipAddress();
    }

    const auto len = snprintf(
        printBuffer_,
        kMaxBufferLength,
        "%s; exptime: %s; from: %s",
        ageStr.data(),
        exptimeStr.data(),
        fromStr.data());
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);

    addStrings(
        "META ",
        key,
        " age: ",
        folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
        "; is_transient: 0\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "metaget");
  }
}

void AsciiSerializedReply::prepareImpl(
    McLeaseGetReply&& reply,
    folly::StringPiece key) {
  const auto valueStr = coalesceAndGetRange(reply.value());

  if (reply.result() == mc_res_found) {
    const auto len = snprintf(
        printBuffer_,
        kMaxBufferLength,
        " %lu %zu\r\n",
        reply.flags(),
        valueStr.size());
    assert(len > 0);
    assert(static_cast<size_t>(len) < kMaxBufferLength);

    addStrings(
        "VALUE ",
        key,
        folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    assert(!iobuf_.hasValue());
    // value was coalesced in coalescedAndGetRange()
    iobuf_ = std::move(reply.value());
    addStrings(valueStr, "\r\n");
  } else if (reply.result() == mc_res_notfound) {
    const auto len = snprintf(
        printBuffer_,
        kMaxBufferLength,
        " %lu %lu %zu\r\n",
        reply.leaseToken(),
        reply.flags(),
        valueStr.size());
    addStrings(
        "LVALUE ",
        key,
        folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
    iobuf_ = std::move(reply.value());
    addStrings(valueStr, "\r\n");
  } else if (reply.result() == mc_res_notfoundhot) {
    addString("NOT_FOUND_HOT\r\n");
  } else if (isErrorResult(reply.result())) {
    LOG(ERROR) << "Got reply result " << reply.result();
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    LOG(ERROR) << "Got unexpected reply result " << reply.result();
    handleUnexpected(reply.result(), "lease-get");
  }
}

// Update-like ops
void AsciiSerializedReply::prepareUpdateLike(
    mc_res_t result,
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

void AsciiSerializedReply::prepareImpl(McSetReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "set");
}

void AsciiSerializedReply::prepareImpl(McAddReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "add");
}

void AsciiSerializedReply::prepareImpl(McReplaceReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "replace");
}

void AsciiSerializedReply::prepareImpl(McAppendReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "append");
}

void AsciiSerializedReply::prepareImpl(McPrependReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "prepend");
}

void AsciiSerializedReply::prepareImpl(McCasReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "cas");
}

void AsciiSerializedReply::prepareImpl(McLeaseSetReply&& reply) {
  prepareUpdateLike(
      reply.result(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "lease-set");
}

void AsciiSerializedReply::prepareArithmeticLike(
    mc_res_t result,
    const uint64_t delta,
    uint16_t errorCode,
    std::string&& message,
    const char* requestName) {
  if (isStoredResult(result)) {
    const auto len = snprintf(printBuffer_, kMaxBufferLength, "%lu\r\n", delta);
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
void AsciiSerializedReply::prepareImpl(McIncrReply&& reply) {
  prepareArithmeticLike(
      reply.result(),
      reply.delta(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "incr");
}

void AsciiSerializedReply::prepareImpl(McDecrReply&& reply) {
  prepareArithmeticLike(
      reply.result(),
      reply.delta(),
      reply.appSpecificErrorCode(),
      std::move(reply.message()),
      "decr");
}

// Delete
void AsciiSerializedReply::prepareImpl(McDeleteReply&& reply) {
  if (reply.result() == mc_res_deleted) {
    addString("DELETED\r\n");
  } else if (reply.result() == mc_res_notfound) {
    addString("NOT_FOUND\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "delete");
  }
}

// Touch
void AsciiSerializedReply::prepareImpl(McTouchReply&& reply) {
  if (reply.result() == mc_res_touched) {
    addString("TOUCHED\r\n");
  } else if (reply.result() == mc_res_notfound) {
    addString("NOT_FOUND\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "touch");
  }
}

// Version
void AsciiSerializedReply::prepareImpl(McVersionReply&& reply) {
  if (reply.result() == mc_res_ok) {
    // TODO(jmswen) Do something sane when version is empty
    addString("VERSION ");
    if (!reply.value().empty()) {
      const auto valueStr = coalesceAndGetRange(reply.value());
      assert(!iobuf_.hasValue());
      // value was coalesced in coalesceAndGetRange()
      iobuf_ = std::move(reply.value());
      addString(valueStr);
    }
    addString("\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "version");
  }
}

// Stats
void AsciiSerializedReply::prepareImpl(McStatsReply&& reply) {
  if (reply.result() == mc_res_ok) {
    if (!reply.stats().empty()) {
      auxString_ = folly::join("\r\n", reply.stats());
      addStrings(*auxString_, "\r\n");
    }
    addString("END\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "stats");
  }
}

// FlushAll
void AsciiSerializedReply::prepareImpl(McFlushAllReply&& reply) {
  if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else { // Don't handleUnexpected(), just return OK
    addString("OK\r\n");
  }
}

// FlushRe
void AsciiSerializedReply::prepareImpl(McFlushReReply&& reply) {
  if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else { // Don't handleUnexpected(), just return OK
    addString("OK\r\n");
  }
}

// Exec
void AsciiSerializedReply::prepareImpl(McExecReply&& reply) {
  if (reply.result() == mc_res_ok) {
    if (!reply.response().empty()) {
      auxString_ = std::move(reply.response());
      addStrings(*auxString_, "\r\n");
    } else {
      addString("OK\r\n");
    }
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "exec");
  }
}

// Shutdown
void AsciiSerializedReply::prepareImpl(McShutdownReply&& reply) {
  if (reply.result() == mc_res_ok) {
    addString("OK\r\n");
  } else if (isErrorResult(reply.result())) {
    handleError(
        reply.result(),
        reply.appSpecificErrorCode(),
        std::move(reply.message()));
  } else {
    handleUnexpected(reply.result(), "shutdown");
  }
}
}
} // facebook::memcache
