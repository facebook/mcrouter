/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsciiSerialized.h"

namespace facebook { namespace memcache {

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
void AsciiSerializedRequest::keyValueRequestCommon(folly::StringPiece prefix,
                                                   const Request& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %d %zd\r\n",
                      request.flags(), request.exptime(), value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(prefix, request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
             value, "\r\n");
}

// Get-like ops.
void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McGetRequest>& request) {
  addStrings("get ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McGetsRequest>& request) {
  addStrings("gets ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McMetagetRequest>& request) {
  addStrings("metaget ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McLeaseGetRequest>& request) {
  addStrings("lease-get ", request.fullKey(), "\r\n");
}

// Update-like ops.
void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McSetRequest>& request) {
  keyValueRequestCommon("set ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McAddRequest>& request) {
  keyValueRequestCommon("add ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McReplaceRequest>& request) {
  keyValueRequestCommon("replace ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McAppendRequest>& request) {
  keyValueRequestCommon("append ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McPrependRequest>& request) {
  keyValueRequestCommon("prepend ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McCasRequest>& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %d %zd %lu\r\n",
                      request.flags(), request.exptime(), value.size(),
                      request->get_casToken());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("cas ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McLeaseSetRequest>& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %lu %d %zd\r\n",
                      request->get_leaseToken(), request.flags(),
                      request.exptime(), value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("lease-set ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

// Arithmetic ops.
void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McIncrRequest>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request->get_delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("incr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McDecrRequest>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request->get_delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("decr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Delete op.
void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McDeleteRequest>& request) {
  addStrings("delete ", request.fullKey());
  if (request.exptime() != 0) {
    auto len = snprintf(printBuffer_, kMaxBufferLength, " %d\r\n",
                        request.exptime());
    assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  } else {
    addString("\r\n");
  }
}

// Touch op.
void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McTouchRequest>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %u\r\n",
                      request.exptime());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("touch ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Version op.
void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McVersionRequest>& request) {
  addString("version\r\n");
}

// FlushAll op.

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McFlushAllRequest>& request) {
  addString("flush_all");
  if (const auto* delay = request->get_delay()) {
    auto len = snprintf(printBuffer_, kMaxBufferLength, " %u",
                        *delay);
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
    addStrings(*auxString_, "\r\n");
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
      assert(!iobuf_.hasValue());
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
    assert(!iobuf_.hasValue());
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
    if (reply->get_age() && *reply->get_age() != -1) {
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
    assert(!iobuf_.hasValue());
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
      assert(!iobuf_.hasValue());
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
    if (reply->get_stats() && !reply->get_stats()->empty()) {
      auxString_ = folly::join("\r\n", *reply->get_stats());
      addStrings(*auxString_, "\r\n");
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
      addStrings(*auxString_, "\r\n");
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

}} // facebook::memcache
