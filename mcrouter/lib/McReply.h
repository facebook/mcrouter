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

#include <folly/io/IOBuf.h>
#include <folly/Memory.h>
#include <folly/Optional.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/Reply.h"

namespace facebook { namespace memcache {

class McReply;
struct AccessPoint;

namespace detail {
inline void mcReplySetMcMsgRef(McReply& reply, McMsgRef&& msg);
}  // detail

/**
 * mc_msg_t-based Reply implementation.
 */
class McReply {
 public:

  /* Reply interface */

  /**
   * Constructs a default successful reply for a given operation.
   *
   * Example uses would be an immediate reply for an async operation;
   * a reply for a delete queued for replay, etc.
   */
  template <int op>
  McReply(DefaultReplyT, McOperation<op>) noexcept;

  template <class Request>
  McReply(DefaultReplyT, const Request&) noexcept;

  /**
   * Constructs an "error" reply, meaning that there was a routing error.
   */
  explicit McReply(ErrorReplyT) noexcept : McReply(mc_res_local_error) {
  }

  /**
   * Same as above, but additionally stores error value.
   */
  McReply(ErrorReplyT, folly::StringPiece valueToSet)
      : McReply(mc_res_local_error, valueToSet) {
  }

  /**
   * Constructs a TKO reply.
   *
   * Used to signal that the Route Handle didn't attempt to send out a request.
   * A sending Route Handle might attempt an immediate failover on a TKO reply.
   */
  explicit McReply(TkoReplyT) noexcept
      : McReply(mc_res_tko) {
  }

  /**
   * Picks one McReply from the iterator range.
   *
   * Used to reduce replies for AllSync and similar.
   *
   * @param begin Points to the first McReply object in the range
   * @param end Points to the first McReply past the end of the range
   *
   * @return Iterator to one of the objects from the input range
   */
  template <typename InputIterator>
  static InputIterator reduce(InputIterator begin, InputIterator end);

  /**
   * @return True if this reply's result is worse than other.result()
   */
  bool worseThan(const McReply& other) const noexcept;
  bool worseThan(mc_res_t otherResult) const noexcept;

  bool isError() const noexcept {
    return isErrorResult(result_);
  }

  bool isFailoverError() const noexcept {
    return isFailoverErrorResult(result_);
  }

  bool isSoftTkoError() const noexcept {
    return isSoftTkoErrorResult(result_);
  }

  bool isHardTkoError() const noexcept {
    return isHardTkoErrorResult(result_);
  }

  bool isTko() const noexcept {
    return isTkoResult(result_);
  }

  bool isLocalError() const noexcept {
    return isLocalErrorResult(result_);
  }

  bool isConnectError() const noexcept {
    return isConnectErrorResult(result_);
  }

  bool isConnectTimeout() const noexcept {
    return isConnectTimeoutResult(result_);
  }

  bool isDataTimeout() const noexcept {
    return isDataTimeoutResult(result_);
  }

  bool isRedirect() const noexcept {
    return isRedirectResult(result_);
  }

  bool isHit() const noexcept {
    return isHitResult(result_);
  }

  bool isMiss() const noexcept {
    return isMissResult(result_);
  }

  bool isHotMiss() const noexcept {
    return isHotMissResult(result_);
  }

  bool isStored() const noexcept {
    return isStoredResult(result_);
  }

  /**
   * Functions to update value and result
   */
  void setValue(folly::IOBuf valueData);
  void setValue(folly::StringPiece str);
  void setResult(mc_res_t res) noexcept {
    result_ = res;
  }

  mc_res_t result() const noexcept {
    return result_;
  }

  /**
   * TypedThriftReply allows syntax like
   *   reply->get_value()
   * In order to ease the transition to TypedThriftReply, we hack McReply
   * to allow similar syntax.
   */
  const McReply* operator->() const {
    return this;
  }

  McReply* operator->() {
    return this;
  }

  bool hasValue() const {
    return valueData_.hasValue();
  }

  const folly::IOBuf& value() const {
    static folly::IOBuf emptyBuffer;
    return hasValue() ? valueData_.value() : emptyBuffer;
  }

  /**
   * Hack. TypedThriftReply comment above applies here, as well.
   */
  const folly::IOBuf& get_value() const {
    return value();
  }

  folly::IOBuf* valuePtrUnsafe() {
    return valueData_.get_pointer();
  }

  const folly::IOBuf* valuePtrUnsafe() const {
    return valueData_.get_pointer();
  }

  folly::StringPiece valueRangeSlow() const {
    return valueData_.hasValue() ? folly::StringPiece(valueData_->coalesce())
                                 : folly::StringPiece();
  }

  const std::shared_ptr<const AccessPoint>& destination() const noexcept {
    return destination_;
  }

  void setDestination(std::shared_ptr<const AccessPoint> ap) noexcept {
    destination_ = std::move(ap);
  }

  uint32_t appSpecificErrorCode() const noexcept {
    return errCode_;
  }

  void setAppSpecificErrorCode(uint32_t ecode) noexcept {
    errCode_ = ecode;
  }

  uint64_t flags() const noexcept {
    return flags_;
  }

  void setFlags(uint64_t fl) noexcept {
    flags_ = fl;
  }

  uint32_t exptime() const noexcept {
    return exptime_;
  }

  void setExptime(uint32_t et) noexcept {
    exptime_ = et;
  }

  uint32_t number() const noexcept {
    return number_;
  }

  void setNumber(uint32_t num) noexcept {
    number_ = num;
  }

  uint64_t leaseToken() const noexcept {
    return leaseToken_;
  }

  void setLeaseToken(uint64_t lt) noexcept {
    leaseToken_ = lt;
  }

  uint64_t cas() const noexcept {
    return cas_;
  }

  void setCas(uint64_t c) noexcept {
    cas_ = c;
  }

  uint64_t delta() const noexcept {
    return delta_;
  }

  void setDelta(uint64_t d) noexcept {
    delta_ = d;
  }

  uint8_t ipv() const {
    return msg_.get() ? msg_->ipv : 0;
  }

  const struct in6_addr& ipAddress() const {
    static struct in6_addr addr;
    return msg_.get() ? msg_->ip_addr : addr;
  }

  void setIpAddress(const struct in6_addr& addr, uint8_t ipVersion) {
    // TODO(prateekj): Task 8315662 - Store ipAddress in McReply itself
    auto msg = createMcMsgRef();
    msg->ip_addr = addr;
    msg->ipv = ipVersion;
    msg_ = std::move(msg);
  }

  /**
   * Hacks for compatibility with TypedThriftReply API. Use sparingly.
   */
  void set_casToken(uint64_t c) noexcept {
    setCas(c);
  }
  void set_leaseToken(uint64_t lt) noexcept {
    setLeaseToken(lt);
  }
  void set_delta(uint64_t d) noexcept {
    setDelta(d);
  }
  void set_exptime(uint64_t et) noexcept {
    setExptime(et);
  }
  void set_age(uint64_t a) noexcept {
    setNumber(a);
  }

  /**
   * Fills out a provided mc_msg_t so that it represents this McReply
   * for the given op.
   * The msg fields might reference the data owned by this McReply,
   * so the msg is only valid as long as the McReply is valid.
   */
  void dependentMsg(mc_op_t op, mc_msg_t* out) const;

  /**
   * Returns a self-contained mc_msg_t representing this McReply
   * for the given op.
   *
   * NOTE: this McReply is still valid after the call.  The obtained
   * McMsgRef and the existing McReply do not depend on each other.
   */
  McMsgRef releasedMsg(mc_op_t op) const;

  ~McReply() = default;

  explicit McReply(mc_res_t result) noexcept;
  McReply() = default;
  McReply(mc_res_t result, McMsgRef&& reply);

  McReply(mc_res_t result, folly::IOBuf value);
  McReply(mc_res_t result, folly::StringPiece value);
  McReply(mc_res_t result, const char* value);
  McReply(mc_res_t result, std::string value);
  McReply(McReply&& other) = default;
  McReply& operator=(McReply&& other) = default;

 private:
  McMsgRef msg_;
  mc_res_t result_{mc_res_unknown};
  mutable folly::Optional<folly::IOBuf> valueData_;
  std::shared_ptr<const AccessPoint> destination_;
  uint64_t flags_{0};
  uint64_t leaseToken_{0};
  uint64_t delta_{0};
  uint64_t cas_{0};
  uint32_t errCode_{0};
  uint32_t number_{0};
  uint32_t exptime_{0};

  friend class McClientAsciiParser;
  inline friend void detail::mcReplySetMcMsgRef(McReply& reply, McMsgRef&& msg);
};

}}  // facebook::memcache

#include "McReply-inl.h"
