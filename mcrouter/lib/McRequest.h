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
#include <folly/Range.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/Keys.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McOperation.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

namespace facebook { namespace memcache {

/**
 * As far as the routing module is concerned, a Request has
 * a routingKey() and an optional routingPrefix(),
 * potentially with other opaque fields.
 * A Reply is similarly opaque.
 *
 * The concrete implementation below uses mc_msg_t as the backing storage
 * level for Requests/Replies.  We also minimize the number of times
 * we have to copy/allocate new mc_msg_ts
 */
class McRequest {
 public:

  /* Request interface */

  /**
   * Useful for incremental construction, i.e. during parsing.
   */
  McRequest() = default;

  McRequest(McRequest&& other) noexcept = default;
  McRequest& operator=(McRequest&& other) = default;

  ~McRequest();

  McRequest clone() const {
    return *this;
  }

  McRequest* operator->() {
    return this;
  }

  /**
   * The routing prefix.
   * Valid as long as this Request object exists.
   */
  folly::StringPiece routingPrefix() const {
    return keys_.routingPrefix();
  }

  /**
   * The routing key used in consistent hashing.
   * Valid as long as this Request object exists.
   */
  folly::StringPiece routingKey() const {
    return keys_.routingKey();
  }

  /**
   * Hashes the routing part of the key (using SpookyHashV2).
   * Used for probabilistic decisions, like stats sampling or shadowing.
   */
  uint32_t routingKeyHash() const {
    return keys_.routingKeyHash();
  }

  /**
   * @return true if "|#|" is present
   */
  bool hasHashStop() const {
    return keys_.routingKey().size() != keys_.keyWithoutRoute().size();
  }

  /**
   * mutator functions
   */
  void setExptime(int32_t expt) {
    exptime_ = expt;
  }
  void setKey(folly::StringPiece k) {
    keyData_ = folly::IOBuf(folly::IOBuf::COPY_BUFFER, k);
    keyData_.coalesce();
    keys_.update(getRange(keyData_));
  }
  void setKey(folly::IOBuf keyData) {
    keyData_ = std::move(keyData);
    keyData_.coalesce();
    keys_.update(getRange(keyData_));
  }
  void stripRoutingPrefix() {
    keyData_.trimStart(keys_.routingPrefix().size());
    keys_.clearRoutingPrefix();
  }
  void setValue(folly::IOBuf valueData) {
    valueData_ = std::move(valueData);
  }
  void setFlags(uint64_t f) {
    flags_ = f;
  }

  /* mc_msg_t specific */

  /**
   * Constructs a request from an existing mc_msg_t object (must not be null).
   * Will hold a reference to the object for the duration of
   * McRequest's life.
   * Note: McRequest assumes that the object will not be modified
   * (and will not modify it itself).
   */
  explicit McRequest(McMsgRef&& msg);

  /**
   * Constructs an McRequest with the given full key
   */
  explicit McRequest(folly::StringPiece key);

  /**
   * Constructs an McRequest with the given key data
   */
  explicit McRequest(folly::IOBuf keyData);

  /**
   * Full key without any routing prefix.
   * Note: differs from routing_key() if "|#|" is present.
   * routing_key() returns only the hashable part, where this method returns
   * the full key without the router_pool
   */
  folly::StringPiece keyWithoutRoute() const;

  /**
   * Access exptime
   */
  int32_t exptime() const;

  /**
   * Access flush_all delay interval.
   */
  uint32_t number() const {
    return number_;
  }

  void setNumber(uint32_t num) {
    number_ = num;
  }

  /**
   * Access flags
   */
  uint64_t flags() const;

  /**
   * Access delta
   */
  uint64_t delta() const {
    return delta_;
  }

  void setDelta(uint64_t d) {
    delta_ = d;
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

  /**
   * Hacks for compatibility with TypedThriftRequest API. Use sparingly.
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
  void set_delay(uint32_t d) noexcept {
    setNumber(d);
  }

  /**
   * @return Full key, including the routing prefix and
   *         non-hashable parts if present
   */
  folly::StringPiece fullKey() const {
    return getRange(keyData_);
  }

  const folly::IOBuf& value() const & {
    return valueData_;
  }

  folly::IOBuf value() && {
    return std::move(valueData_);
  }

  /**
   * Hack to make interface of McRequestWithOp compatible with
   * TypedThriftRequest in a few places, such as BigValueRoute.
   */
  const folly::IOBuf& get_value() const {
    return value();
  }

  folly::StringPiece valueRangeSlow() const {
    return folly::StringPiece(valueData_.coalesce());
  }

  const folly::IOBuf& key() const & {
    return keyData_;
  }

  folly::IOBuf key() && {
    return std::move(keyData_);
  }

#ifndef LIBMC_FBTRACE_DISABLE
  mc_fbtrace_info_s* fbtraceInfo() const {
    return fbtraceInfo_.get();
  }

  /**
   * Note: will not incref info, it's up to the caller.
   */
  void setFbtraceInfo(mc_fbtrace_info_s* info) {
    fbtraceInfo_ = McFbtraceRef::moveRef(info);
  }
#endif

 private:
  /* Always stored unchained */
  folly::IOBuf keyData_;

  /* May be chained */
  mutable folly::IOBuf valueData_;

  /**
   * Holds all the references to the various parts of the key.
   *
   *                        /region/cluster/foo:key|#|etc
   * keyData_:              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   * keyWithoutRoute:                       ^^^^^^^^^^^^^
   * routingPrefix:         ^^^^^^^^^^^^^^^^
   * routingKey:                            ^^^^^^^
   */
  Keys keys_;

  int32_t exptime_{0};
  uint32_t number_{0};
  uint64_t flags_{0};
  uint64_t delta_{0};
  uint64_t leaseToken_{0};
  uint64_t cas_{0};

#ifndef LIBMC_FBTRACE_DISABLE
  struct McFbtraceRefPolicy {
    struct Deleter {
      void operator()(mc_fbtrace_info_t* info) const {
        mc_fbtrace_info_decref(info);
      }
    };

    static mc_fbtrace_info_t* increfOrNull(
      mc_fbtrace_info_t* info) {
      return mc_fbtrace_info_incref(info);
    }

    static void decref(mc_fbtrace_info_t* info) {
      mc_fbtrace_info_decref(info);
    }
  };
  using McFbtraceRef = Ref<mc_fbtrace_info_t, McFbtraceRefPolicy>;
  McFbtraceRef fbtraceInfo_;
#endif

  McRequest(const McRequest& other);

  /**
   * Direct key/value construction from the parsing code for efficiency.
   */
  friend McRequest umbrellaParseRequest(const folly::IOBuf& source,
                                        const uint8_t*, size_t,
                                        const uint8_t*, size_t,
                                        mc_op_t&, uint64_t&);
  /**
   * Clone the key from the subregion of source [begin, begin + size).
   * @return false If the subregion is empty or not valid (i.e. not contained
   *   within [source.data(), source.data() + length())
   */
  bool setKeyFrom(const folly::IOBuf& source,
                  const uint8_t* keyBegin, size_t keySize);

  /**
   * Clone the key from the subregion of source [begin, begin + size).
   * @return false If the subregion is empty or not valid (i.e. not contained
   *   within [source.data(), source.data() + length())
   */
  bool setValueFrom(const folly::IOBuf& source,
                    const uint8_t* valueBegin, size_t valueSize);

  friend class McServerAsciiParser;

  template <class Operation>
  friend class McRequestWithOp;
};

/*
 * TODO(jmswen) Change all instances of `McRequest` to `McRequestWithOp`.
 *
 * The coexistence of `McRequest` and `McRequestWithOp` (which copies the
 * API of `McRequest` and merely forwards all work to its `McRequest` member)
 * is a temporary hack. New code should use `McRequestWithOp`, if feasible.
 * This hack eases development for support for custom Thrift structures, which
 * inherently have the notion of `Operation` built into each request type.
 */
template <class Operation>
class McRequestWithOp {
 public:
  using OpType = Operation;
  static const char* const name;

  McRequestWithOp() = default;
  explicit McRequestWithOp(McMsgRef&& msg)
    : req_(std::move(msg)) {}
  explicit McRequestWithOp(folly::StringPiece k)
    : req_(k) {}
  explicit McRequestWithOp(folly::IOBuf keyData)
    : req_(std::move(keyData)) {}
  template <typename OtherOp>
  /* impilcit */ McRequestWithOp(McRequestWithOp<OtherOp>&& other)
    : req_(std::move(other.req_)) {}

  McRequestWithOp& operator=(const McRequestWithOp&) = delete;

  McRequestWithOp(McRequestWithOp&& other) noexcept = default;
  McRequestWithOp& operator=(McRequestWithOp&& other) = default;

  explicit McRequestWithOp(McRequest&& other) noexcept
    : req_(std::move(other)) {}

  ~McRequestWithOp() = default;

  McRequestWithOp clone() const {
    return *this;
  }

  /**
   * Hack to make interface of McRequestWithOp compatible with
   * TypedThriftRequest in a few places, such as BigValueRoute.
   */
  const McRequest* operator->() const {
    return &req_;
  }

  McRequest moveMcRequest() {
    return std::move(req_);
  }

  folly::StringPiece routingPrefix() const {
    return req_.routingPrefix();
  }
  folly::StringPiece routingKey() const {
    return req_.routingKey();
  }
  uint32_t routingKeyHash() const {
    return req_.routingKeyHash();
  }
  bool hasHashStop() const {
    return req_.hasHashStop();
  }
  void setExptime(int32_t expt) {
    req_.setExptime(expt);
  }
  void setKey(folly::StringPiece k) {
    req_.setKey(k);
  }
  void setKey(folly::IOBuf keyData) {
    req_.setKey(std::move(keyData));
  }
  void stripRoutingPrefix() {
    req_.stripRoutingPrefix();
  }
  void setValue(folly::IOBuf valueData) {
    req_.setValue(std::move(valueData));
  }
  void setFlags(uint64_t f) {
    req_.setFlags(f);
  }

  folly::StringPiece keyWithoutRoute() const {
    return req_.keyWithoutRoute();
  }

  int32_t exptime() const {
    return req_.exptime();
  }
  uint32_t number() const {
    return req_.number();
  }
  void setNumber(uint32_t num) {
    req_.setNumber(num);
  }
  uint64_t flags() const {
    return req_.flags();
  }
  uint64_t delta() const {
    return req_.delta();
  }
  void setDelta(uint64_t d) {
    req_.setDelta(d);
  }
  uint64_t leaseToken() const {
    return req_.leaseToken();
  }
  void setLeaseToken(uint64_t lt) {
    req_.setLeaseToken(lt);
  }
  uint64_t cas() const {
    return req_.cas();
  }
  void setCas(uint64_t c) {
    req_.setCas(c);
  }
  folly::StringPiece fullKey() const {
    return req_.fullKey();
  }
  const folly::IOBuf& value() const {
    return req_.value();
  }
  const folly::IOBuf* valuePtrUnsafe() const {
    return &value();
  }
  folly::StringPiece valueRangeSlow() const {
    return req_.valueRangeSlow();
  }
  const folly::IOBuf& key() const {
    return req_.key();
  }
#ifndef LIBMC_FBTRACE_DISABLE
  mc_fbtrace_info_s* fbtraceInfo() const {
    return req_.fbtraceInfo();
  }
  void setFbtraceInfo(mc_fbtrace_info_s* info) {
    req_.setFbtraceInfo(info);
  }
#endif

 private:
  McRequest req_;

  McRequestWithOp(const McRequestWithOp&) = default;

  template <class OtherOp>
  friend class McRequestWithOp;
};

// Initialize from Operation::mc_op instead of Operation::name to avoid SIOF.
template <class Operation>
const char* const McRequestWithOp<Operation>::name =
  mc_op_to_string(Operation::mc_op);

template <int op>
using McRequestWithMcOp = McRequestWithOp<McOperation<op>>;

}}  // facebook::memcache
