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

#include <memory>
#include <string>
#include <type_traits>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/Keys.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/network/detail/RequestUtil.h"
#include "mcrouter/lib/network/RawThriftMessageTraits.h"
#include "mcrouter/lib/network/ThriftMessageTraits.h"
#include "mcrouter/lib/Reply.h"

namespace facebook { namespace memcache {

struct AccessPoint;

/**
 * A thin wrapper for Thrift structs
 */
template <class M>
class TypedThriftMessage {
 public:
  using rawType = M;

  M& operator*() {
    return raw_;
  }

  const M& operator*() const {
    return raw_;
  }

  M* operator->() {
    return &raw_;
  }

  const M* operator->() const {
    return &raw_;
  }

  M* get() {
    return &raw_;
  }

  const M* get() const {
    return &raw_;
  }

  template <class Protocol>
  uint32_t read(Protocol* iprot) {
    return raw_.read(iprot);
  }

 protected:
  M raw_;
};

template <class M>
class TypedThriftReply : public TypedThriftMessage<M> {
 public:
  TypedThriftReply() = default;
  TypedThriftReply(TypedThriftReply&&) = default;
  TypedThriftReply& operator=(TypedThriftReply&&) = default;

  explicit TypedThriftReply(mc_res_t res) noexcept {
    this->raw_.set_result(res);
  }

  TypedThriftReply(mc_res_t res, const char* msg) {
    this->raw_.set_result(res);
    this->raw_.set_message(msg);
  }

  TypedThriftReply(mc_res_t res, std::string msg) {
    this->raw_.set_result(res);
    this->raw_.set_message(std::move(msg));
  }

  template <class Request>
  TypedThriftReply(DefaultReplyT, const Request&,
                   UpdateLikeT<Request> = 0) noexcept {
    this->raw_.set_result(mc_res_notstored);
  }

  template <class Request>
  TypedThriftReply(DefaultReplyT, const Request&,
                   OtherThanT<Request, UpdateLike<>> = 0) noexcept {
    this->raw_.set_result(mc_res_notfound);
  }

  explicit TypedThriftReply(ErrorReplyT) noexcept {
    this->raw_.set_result(mc_res_local_error);
  }

  TypedThriftReply(ErrorReplyT, folly::StringPiece msg) {
    this->raw_.set_result(mc_res_local_error);
    this->raw_.set_message(msg.str());
  }

  explicit TypedThriftReply(TkoReplyT) noexcept {
    this->raw_.set_result(mc_res_tko);
  }

  /**
   * Picks one TypedThriftReply from the iterator range.
   *
   * Used to reduce replies for AllSync and similar.
   *
   * @param begin Points to the first TypedThriftReply object in the range
   * @param end Points to the first TypedThriftReply past the end of the range
   *
   * @return Iterator to one of the objects from the input range
   */
  template <typename InputIterator>
  static InputIterator reduce(InputIterator begin, InputIterator end);

  mc_res_t result() const noexcept {
    return static_cast<mc_res_t>(this->raw_.get_result());
  }

  void setResult(mc_res_t res) noexcept {
    this->raw_.set_result(res);
  }

  bool hasValue() const noexcept {
    return valuePtrUnsafe() != nullptr;
  }

  folly::IOBuf* valuePtrUnsafe() noexcept {
    return const_cast<folly::IOBuf*>(
        detail::valuePtrUnsafe(const_cast<const TypedThriftReply&>(*this)));
  }

  const folly::IOBuf* valuePtrUnsafe() const noexcept {
    return detail::valuePtrUnsafe(*this);
  }

  folly::StringPiece valueRangeSlow() const {
    auto* valuePtr = const_cast<folly::IOBuf*>(valuePtrUnsafe());

    return valuePtr ? folly::StringPiece(valuePtr->coalesce())
                    : folly::StringPiece();
  }

  void setValue(folly::IOBuf valueData) noexcept {
    detail::setValue(*this, std::move(valueData));
  }

  void setValue(folly::StringPiece str) {
    detail::setValue(*this, folly::IOBuf(folly::IOBuf::COPY_BUFFER, str));
  }

  uint64_t flags() const noexcept {
    return detail::flags(*this);
  }

  void setFlags(uint64_t f) noexcept {
    return detail::setFlags(*this, f);
  }

  const std::shared_ptr<const AccessPoint>& destination() const noexcept {
    return destination_;
  }

  void setDestination(std::shared_ptr<const AccessPoint> ap) noexcept {
    destination_ = std::move(ap);
  }

  uint16_t appSpecificErrorCode() const noexcept {
    const auto errorCode = this->raw_.get_appSpecificErrorCode();
    return errorCode ? *errorCode : 0;
  }

  void setAppSpecificErrorCode(uint16_t c) {
    this->raw_.set_appSpecificErrorCode(c);
  }

  bool worseThan(const TypedThriftReply& other) const noexcept {
    return resultSeverity(result()) > resultSeverity(other.result());
  }

  bool isError() const noexcept {
    return isErrorResult(result());
  }

  bool isFailoverError() const noexcept {
    return isFailoverErrorResult(result());
  }

  bool isSoftTkoError() const noexcept {
    return isSoftTkoErrorResult(result());
  }

  bool isHardTkoError() const noexcept {
    return isHardTkoErrorResult(result());
  }

  bool isTko() const noexcept {
    return isTkoResult(result());
  }

  bool isLocalError() const noexcept {
    return isLocalErrorResult(result());
  }

  bool isConnectError() const noexcept {
    return isConnectErrorResult(result());
  }

  bool isConnectTimeout() const noexcept {
    return isConnectTimeoutResult(result());
  }

  bool isDataTimeout() const noexcept {
    return isDataTimeoutResult(result());
  }

  bool isRedirect() const noexcept {
    return isRedirectResult(result());
  }

  bool isHit() const noexcept {
    return isHitResult(result());
  }

  bool isMiss() const noexcept {
    return isMissResult(result());
  }

  bool isHotMiss() const noexcept {
    return isHotMissResult(result());
  }

  bool isStored() const noexcept {
    return isStoredResult(result());
  }

 private:
  std::shared_ptr<const AccessPoint> destination_;

  template <class TMList, class Derived, class... Args>
  friend class ThriftMsgDispatcher;
};

template <class M>
class TypedThriftRequest : public TypedThriftMessage<M>,
                           private Keys {
 public:
  static constexpr const char* name = RequestTraits<M>::name;

  TypedThriftRequest() = default;

  TypedThriftRequest clone() const {
    return *this;
  }

  TypedThriftRequest& operator=(const TypedThriftRequest& other) = delete;

  TypedThriftRequest(TypedThriftRequest&& other) noexcept = default;
  TypedThriftRequest& operator=(TypedThriftRequest&& other) = default;

  explicit TypedThriftRequest(folly::IOBuf k) noexcept {
    this->raw_.set_key(std::move(k));
  }

  explicit TypedThriftRequest(folly::StringPiece k) {
    this->raw_.set_key(folly::IOBuf(folly::IOBuf::COPY_BUFFER, k));
  }

  const folly::IOBuf& key() const {
    return this->raw_.key;
  }

  folly::StringPiece fullKey() const {
    return getRange(this->raw_.key);
  }

  void setKey(folly::StringPiece k) {
    this->raw_.set_key(folly::IOBuf(folly::IOBuf::COPY_BUFFER, k));
    Keys::update(getRange(this->raw_.key));
  }

  void setKey(folly::IOBuf k) {
    this->raw_.set_key(std::move(k));
    this->raw_.key.coalesce();
    Keys::update(getRange(this->raw_.key));
  }

  void stripRoutingPrefix() {
    this->raw_.key.trimStart(routingPrefix().size());
    Keys::clearRoutingPrefix();
  }

  folly::StringPiece keyWithoutRoute() const {
    return Keys::keyWithoutRoute();
  }

  folly::StringPiece routingKey() const {
    return Keys::routingKey();
  }

  folly::StringPiece routingPrefix() const {
    return Keys::routingPrefix();
  }

  uint32_t routingKeyHash() const {
    return Keys::routingKeyHash();
  }

  bool hasHashStop() const {
    return Keys::routingKey().size() != Keys::keyWithoutRoute().size();
  }

  uint32_t exptime() const {
    return detail::exptime(*this);
  }

  void setExptime(int32_t expt) {
    detail::setExptime(*this, expt);
  }

  const folly::IOBuf* valuePtrUnsafe() const {
    return detail::valuePtrUnsafe(*this);
  }

  void setValue(folly::IOBuf valueData) {
    detail::setValue(*this, std::move(valueData));
  }

  void setValue(folly::StringPiece str) {
    detail::setValue(*this, folly::IOBuf(folly::IOBuf::COPY_BUFFER, str));
  }

  /* Treat value IOBuf as mutable, as in McRequest */
  folly::StringPiece valueRangeSlow() const {
    auto* valuePtr = const_cast<folly::IOBuf*>(valuePtrUnsafe());

    return valuePtr ? folly::StringPiece(valuePtr->coalesce())
                    : folly::StringPiece();
  }

  uint64_t flags() const {
    return detail::flags(*this);
  }

  void setFlags(uint64_t f) {
    detail::setFlags(*this, f);
  }

 private:
  TypedThriftRequest(const TypedThriftRequest& other) = default;

  template <class TMList, class Derived, class... Args>
  friend class ThriftMsgDispatcher;
};

}}  // facebook::memcache

#include "TypedThriftMessage-inl.h"
