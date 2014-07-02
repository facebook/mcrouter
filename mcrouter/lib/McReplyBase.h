/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>

#include "folly/io/IOBuf.h"
#include "folly/Memory.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McMsgRef.h"

namespace facebook { namespace memcache {

/**
 * mc_msg_t-based Reply implementation.
 */
class McReplyBase {
 public:

  /* Reply interface */

  /**
   * Constructs a default successful reply for a given operation.
   *
   * Example uses would be an immediate reply for an async operation;
   * a reply for a delete queued for replay, etc.
   */
  template <typename Operation>
  static McReplyBase defaultReply(Operation op);

  /**
   * Constructs an "error" reply, meaning that there was a routing error.
   */
  static McReplyBase errorReply(folly::StringPiece valueToSet="");

  /**
   * Constructs a TKO reply.
   *
   * Used to signal that the Route Handle didn't attempt to send out a request.
   * A sending Route Handle might attempt an immediate failover on a TKO reply.
   */
  static McReplyBase tkoReply();

  /**
   * Picks one McReplyBase from the iterator range.
   *
   * Used to reduce replies for AllSync and similar.
   *
   * @param begin Points to the first McReplyBase object in the range
   * @param end Points to the first McReplyBase past the end of the range
   *
   * @return Iterator to one of the objects from the input range
   */
  template <typename InputIterator>
  static InputIterator reduce(InputIterator begin, InputIterator end);

  /**
   * @return True if this reply's result is worse than other.result()
   */
  bool worseThan(const McReplyBase& other) const;

  /**
   * Is this reply an error as far as failover logic is concerned?
   */
  bool isError() const;

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
   * Did we receieve a mc_res_try_again from the server.
   */
  bool isTryAgain() {
    return result_ == mc_res_try_again;
  }

  /**
   * Did we receieve a mc_res_busy from the server.
   */
  bool isBusy() {
    return result_ == mc_res_busy;
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
    /* isError() here is needed because some mc_res_remote_errors are not
       errors */
    return isError() && (
      result_ == mc_res_timeout || result_ == mc_res_remote_error);
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
   * Was the data stored?
   */
  bool isStored() const {
    return result_ == mc_res_stored || result_ == mc_res_stalestored;
  }

  /**
   * Functions to update value and result
   */
  void setValue(std::unique_ptr<folly::IOBuf> valueData);
  void setValue(folly::StringPiece str);
  void setResult(mc_res_t res);

  /* mc_msg_t specific */

  explicit McReplyBase(mc_res_t result);
  McReplyBase(mc_res_t result, McMsgRef&& reply);
  McReplyBase(mc_res_t result, std::unique_ptr<folly::IOBuf> value);
  McReplyBase(mc_res_t res, folly::StringPiece val);
  McReplyBase(McReplyBase&& other) noexcept = default;
  McReplyBase& operator=(McReplyBase&& other) = default;
  mc_res_t result() const {
    return result_;
  }

  const folly::IOBuf& value() const {
    static auto emptyIOBuf = folly::IOBuf::create(0);
    return valueData_ ? *valueData_ : *emptyIOBuf;
  }

  uint64_t flags() const {
    return flags_;
  }

  void setFlags(uint64_t fl) {
    flags_ = fl;
  }

  uint64_t leaseToken() const {
    return leaseToken_;
  }

  void setLeaseToken(uint64_t lt) {
    leaseToken_ = lt;
  }

  void setDelta(uint64_t delta) {
    delta_ = delta;
  }

  /**
   * Fills out a provided mc_msg_t so that it represents this McReplyBase
   * for the given op.
   * The msg fields might reference the data owned by this McReply,
   * so the msg is only valid as long as the McReply is valid.
   */
  void dependentMsg(mc_op_t op, mc_msg_t* out) const;

  /**
   * Returns a self-contained mc_msg_t representing this McReplyBase
   * for the given op.
   *
   * NOTE: this McReplyBase is still valid after the call.  The obtained
   * McMsgRef and the existing McReplyBase do not depend on each other.
   */
  McMsgRef releasedMsg(mc_op_t op) const;

  ~McReplyBase() {}

 private:
  McMsgRef msg_;
  mc_res_t result_{mc_res_unknown};
  std::unique_ptr<folly::IOBuf> valueData_;
  uint64_t flags_{0};
  uint64_t leaseToken_{0};
  uint64_t delta_{0};
};

}}  // facebook::memcache

#include "McReplyBase-inl.h"
