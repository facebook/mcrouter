/*
 *  Copyright (c) 2015, Facebook, Inc.
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
  template <typename Operation>
  McReply(DefaultReplyT, Operation op);

  /**
   * Constructs an "error" reply, meaning that there was a routing error.
   */
  explicit McReply(ErrorReplyT, folly::StringPiece valueToSet="")
      : McReply(mc_res_local_error, valueToSet) {
  }

  /**
   * Constructs a TKO reply.
   *
   * Used to signal that the Route Handle didn't attempt to send out a request.
   * A sending Route Handle might attempt an immediate failover on a TKO reply.
   */
  explicit McReply(TkoReplyT)
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
  bool worseThan(const McReply& other) const;

  /**
   * Is this reply an error?
   */
  inline bool isError() const;

  /**
   * Is this reply an error as far as failover logic is concerned?
   */
  inline bool isFailoverError() const;

  /**
   * Is this reply a soft TKO error?
   */
  bool isSoftTkoError() const;

  /**
   * Is this reply a hard TKO error?
   */
  bool isHardTkoError() const;

  /**
   * Did we not even attempt to send request out because at some point
   * we decided the destination is in TKO state?
   *
   * Used to short-circuit failover decisions in certain RouteHandles.
   *
   * If isTko() is true, isError() must also be true.
   */
  bool isTko() const {
    return result_ == mc_res_tko;
  }

  /**
   * Did we not even attempt to send request out because it is invalid/we hit
   * per-destination rate limit
   */
  bool isLocalError() const {
    return result_ == mc_res_local_error;
  }

  /**
   * Was the connection attempt refused?
   */
  bool isConnectError() const {
    return result_ == mc_res_connect_error;
  }

  /**
   * Was there a timeout while attempting to establish a connection?
   */
  bool isConnectTimeout() const {
    return result_ == mc_res_connect_timeout;
  }

  /**
   * Was there a timeout when sending data on an established connection?
   * Note: the distinction is important, since in this case we don't know
   * if the data reached the server or not.
   */
  bool isDataTimeout() const {
    return result_ == mc_res_timeout || result_ == mc_res_remote_error;
  }

  /**
   * Application-specific redirect code. Server is up, but doesn't want
   * to reply now.
   */
  bool isRedirect() const {
    return result_ == mc_res_busy || result_ == mc_res_try_again;
  }

  /**
   * Was the data found?
   */
  bool isHit() const {
    return result_ == mc_res_deleted || result_ == mc_res_found;
  }

  /**
   * Was data not found and no errors occured?
   */
  bool isMiss() const {
    return result_ == mc_res_notfound;
  }

  /**
   * Lease hot miss?
   */
  bool isHotMiss() const {
    return result_ == mc_res_foundstale || result_ == mc_res_notfoundhot;
  }

  /**
   * Was the data stored?
   */
  bool isStored() const {
    return result_ == mc_res_stored || result_ == mc_res_stalestored;
  }

  /**
   * Functions to update value and result
   */
  void setValue(folly::IOBuf valueData);
  void setValue(folly::StringPiece str);
  void setResult(mc_res_t res);

  mc_res_t result() const {
    return result_;
  }

  bool hasValue() const {
    return valueData_.hasValue();
  }

  const folly::IOBuf& value() const {
    static folly::IOBuf emptyBuffer;
    return hasValue() ? valueData_.value() : emptyBuffer;
  }

  folly::StringPiece valueRangeSlow() const {
    return valueData_.hasValue() ? folly::StringPiece(valueData_->coalesce())
                                 : folly::StringPiece();
  }

  const std::shared_ptr<const AccessPoint>& destination() const {
    return destination_;
  }

  void setDestination(std::shared_ptr<const AccessPoint> ap) {
    destination_ = std::move(ap);
  }

  uint32_t appSpecificErrorCode() const {
    return errCode_;
  }

  void setAppSpecificErrorCode(uint32_t ecode) {
    errCode_ = ecode;
  }

  uint64_t flags() const {
    return flags_;
  }

  void setFlags(uint64_t fl) {
    flags_ = fl;
  }

  uint32_t exptime() const {
    return exptime_;
  }

  void setExptime(uint32_t et) {
    exptime_ = et;
  }

  uint32_t number() const {
    return number_;
  }

  void setNumber(uint32_t num) {
    number_ = num;
  }

  uint64_t leaseToken() const {
    return leaseToken_;
  }

  void setLeaseToken(uint64_t lt) {
    leaseToken_ = lt;
  }

  uint64_t cas() const {
    return cas_;
  }

  void setCas(uint64_t c) {
    cas_ = c;
  }

  uint64_t delta() const {
    return delta_;
  }

  void setDelta(uint64_t d) {
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


  /**
   * Set the destructor. If destructor/ctx are non-null, will call
   * destructor(ctx) when this reply is destroyed.
   */
  void setDestructor(void (*destructor) (void*), void* ctx) {
    assert(!destructor_.hasValue());
    destructor_.assign(CUniquePtr(ctx, destructor));
  }

  ~McReply() {};

  explicit McReply(mc_res_t result);
  McReply();
  McReply(mc_res_t result, McMsgRef&& reply);

  McReply(mc_res_t result, folly::IOBuf value);
  McReply(mc_res_t result, folly::StringPiece value);
  McReply(mc_res_t result, const char* value);
  McReply(mc_res_t result, const std::string& value);
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

  /**
   * Container for a C-style destructor
   */
  using CUniquePtr = std::unique_ptr<void, void(*)(void*)>;
  folly::Optional<CUniquePtr> destructor_;

  friend class McAsciiParser;
  inline friend void detail::mcReplySetMcMsgRef(McReply& reply, McMsgRef&& msg);
};

}}  // facebook::memcache

#include "McReply-inl.h"
