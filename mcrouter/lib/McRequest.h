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
#include <folly/Range.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McMsgRef.h"

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

  /**
   * The routing prefix.
   * Valid as long as this Request object exists.
   */
  folly::StringPiece routingPrefix() const {
    return keys_.routingPrefix;
  }

  /**
   * The routing key used in consistent hashing.
   * Valid as long as this Request object exists.
   */
  folly::StringPiece routingKey() const {
    return keys_.routingKey;
  }

  /**
   * Hashes the routing part of the key (using SpookyHashV2).
   * Used for probabilistic decisions, like stats sampling or shadowing.
   */
  uint32_t routingKeyHash() const {
    return keys_.routingKeyHash;
  }

  /**
   * @return true if "|#|" is present
   */
  bool hasHashStop() const {
    return keys_.routingKey.size() != keys_.keyWithoutRoute.size();
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
    keyData_.trimStart(keys_.routingPrefix.size());
    keys_.routingPrefix.clear();
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
   * Returns a reference to an mc_msg_t representing this request
   * with the correct op set.
   *
   * Note: McRequest assumes this object will not be modified.
   *
   * The returned mc_msg_t might reference data owned by this McRequest,
   * so the McRequest must be kept alive (thus "dependent").
   */
  McMsgRef dependentMsg(mc_op_t op) const;

  /**
   * Same as dependentMsg(), but any routing prefix is stripped.
   * If the original request didn't have a routing prefix to begin with,
   * behaves exactly like dependentMsg().
   */
  McMsgRef dependentMsgStripRoutingPrefix(mc_op_t op) const;

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
    return msg_.get() ? msg_->number : 0;
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
   * @return Full key, including the routing prefix and
   *         non-hashable parts if present
   */
  folly::StringPiece fullKey() const {
    return getRange(keyData_);
  }

  const folly::IOBuf& value() const {
    return valueData_;
  }

  folly::StringPiece valueRangeSlow() const {
    return folly::StringPiece(valueData_.coalesce());
  }

  const folly::IOBuf& key() const {
    return keyData_;
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
  McMsgRef msg_;

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
  struct Keys {
    folly::StringPiece keyWithoutRoute;
    folly::StringPiece routingPrefix;
    folly::StringPiece routingKey;

    uint32_t routingKeyHash;

    Keys() {}
    explicit Keys(folly::StringPiece key) noexcept;
    void update(folly::StringPiece key);
  } keys_;

  int32_t exptime_{0};
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

  /**
   * Helper method to set all fields and proper key/value into McMsgRef.
   */
  void dependentHelper(mc_op_t op, folly::StringPiece key,
                       folly::StringPiece value,
                       MutableMcMsgRef& into) const;

  void ensureMsgExists(mc_op_t op) const;

  McRequest(const McRequest& other);
};

}}  // facebook::memcache
