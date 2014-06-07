/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "folly/Range.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McStringData.h"

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
class McRequestBase {
 public:

  /* Request interface */

  McRequestBase(McRequestBase&& other) noexcept = default;
  McRequestBase& operator=(McRequestBase&& other) = default;

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
   * mutator functions
   */
  void setExptime(uint32_t expt) {
    exptime_ = expt;
  }
  void setKey(McStringData&& keyData) {
    keyData_ = std::move(keyData);
    keys_.update(keyData_.dataRange());
  }
  void setValue(McStringData&& valueData) {
    valueData_ = std::move(valueData);
  }
  void setFlags(uint64_t f) {
    flags_ = f;
  }

  /* mc_msg_t specific */

  /**
   * Constructs a request from an existing mc_msg_t object (must not be null).
   * Will hold a reference to the object for the duration of
   * McRequestBase's life.
   * Note: McRequestBase assumes that the object will not be modified
   * (and will not modify it itself).
   */
  explicit McRequestBase(McMsgRef&& msg);

  /**
   * Constructs an McRequestBase with the given full key
   */
  explicit McRequestBase(const std::string& key);

  /**
   * Returns a reference to an mc_msg_t representing this request
   * with the correct op set.
   *
   * Note: McRequestBase assumes this object will not be modified.
   *
   * The returned mc_msg_t might reference data owned by this McRequestBase,
   * so the McRequestBase must be kept alive (thus "dependent").
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
  uint32_t exptime() const;

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

  uint64_t leaseToken() const {
    return leaseToken_;
  }

  /**
   * @return Full key, including the routing prefix and
   *         non-hashable parts if present
   */
  folly::StringPiece fullKey() const {
    return keyData_.dataRange();
  }

  const McStringData& value() const;

 private:
  McMsgRef msg_;
  McStringData keyData_;
  McStringData valueData_;

  /**
   * Holds all the references to the various parts of the key.
   *
   *                        /region/cluster/foo:key|#|etc
   * keyData_.dataRange():  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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

  uint32_t exptime_{0};
  uint64_t flags_{0};
  uint64_t delta_{0};
  uint64_t leaseToken_{0};

 protected:
  McRequestBase(const McRequestBase& other);
  ~McRequestBase() {};
};

}}  // facebook::memcache
