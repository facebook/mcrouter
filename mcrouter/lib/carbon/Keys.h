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

#include <iostream>
#include <string>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>
#include <folly/SpookyHashV2.h>

namespace carbon {

template <class Storage>
Storage makeKey(folly::StringPiece sp);

template <>
inline std::string makeKey<std::string>(folly::StringPiece sp) {
  return sp.str();
}

template <>
inline folly::IOBuf makeKey<folly::IOBuf>(folly::StringPiece sp) {
  return folly::IOBuf(folly::IOBuf::COPY_BUFFER, sp.data(), sp.size());
}

/**
 * Holds all the references to the various parts of the key.
 *
 *                        /region/cluster/foo:key|#|etc
 * keyData_:              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * keyWithoutRoute:                       ^^^^^^^^^^^^^
 * routingPrefix:         ^^^^^^^^^^^^^^^^
 * routingKey:                            ^^^^^^^
 */
template <class Storage>
class Keys {
 public:
  constexpr Keys() = default;

  explicit Keys(Storage&& key)
    : key_(std::move(key)) { update(); }

  explicit Keys(folly::StringPiece sp)
    : key_(makeKey<Storage>(sp)) { update(); }

  Keys(const Keys& other) = default;
  Keys& operator=(const Keys& other) = default;

  Keys& operator=(const Storage& key) {
    key_ = key;
    update();
    return *this;
  }

  Keys& operator=(Storage&& key) {
    key_ = std::move(key);
    update();
    return *this;
  }

  Keys& operator=(folly::StringPiece key) {
    key_ = makeKey<Storage>(key);
    update();
    return *this;
  }

  size_t size() const { return size(key_); }

  bool empty() const { return key_.empty(); }

  folly::StringPiece fullKey() const {
    return {reinterpret_cast<const char*>(key_.data()), size()};
  }
  folly::StringPiece keyWithoutRoute() const { return keyWithoutRoute_; }
  folly::StringPiece routingPrefix() const { return routingPrefix_; }
  folly::StringPiece routingKey() const { return routingKey_; }
  uint32_t routingKeyHash() const {
    if (!routingKeyHash_) {
      const auto keyPiece = fullKey();
      routingKeyHash_ = folly::hash::SpookyHashV2::Hash32(
          keyPiece.begin(), keyPiece.size(), /* seed= */ 0);
    }
    return routingKeyHash_;
  }

  bool hasHashStop() const {
    return routingKey_.size() != keyWithoutRoute_.size();
  }

  // Hack to save some CPU in DestinationRoute. Avoid if possible.
  void stripRoutingPrefix() {
    trimStart(routingPrefix().size());
    routingPrefix_.clear();
  }

  // TODO(jmswen) Remove this hack? Currently only needed in asciiKey()
  // in McServerSession-inl.h and for SerializationTraits specialization.
  const Storage& raw() const {
    return key_;
  }

 private:
  void update();

  static size_t size(const folly::IOBuf& buf) { return buf.length(); }
  static size_t size(const std::string& str) { return str.size(); }

  void trimStart(size_t n) {
    return trimStartImpl(key_, n);
  }

  static void trimStartImpl(folly::IOBuf& buf, size_t n) {
    buf.trimStart(n);
  }

  static void trimStartImpl(std::string& s, size_t n) {
    s.erase(0, n);
  }

 protected:
  Storage key_;

 private:
  folly::StringPiece keyWithoutRoute_;
  folly::StringPiece routingPrefix_;
  folly::StringPiece routingKey_;
  mutable uint32_t routingKeyHash_{0};
};

} //carbon

#include "Keys-inl.h"
