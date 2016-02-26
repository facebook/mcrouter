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

#include <type_traits>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "mcrouter/lib/Keys.h"
#include "mcrouter/lib/network/RawThriftMessageTraits.h"
#include "mcrouter/lib/network/ThriftMessageTraits.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"

namespace facebook { namespace memcache {

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

 protected:
  M raw_;

  template <class Protocol>
  uint32_t read(Protocol* iprot) {
    return raw_.read(iprot);
  }
};

template <class M>
class TypedThriftReply : public TypedThriftMessage<M> {
 private:
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

  folly::StringPiece fullKey() const {
    return getRange(this->raw_.key);
  }

  void setKey(folly::StringPiece k) {
    // TODO(jmswen) Update to use thrift setters, here and elsewhere
    auto& key = this->raw_.key;
    key = folly::IOBuf(folly::IOBuf::COPY_BUFFER, k);
    this->raw_.__isset.key = true;
    Keys::update(getRange(key));
  }

  void setKey(folly::IOBuf k) {
    auto& key = this->raw_.key;
    key = std::move(k);
    this->raw_.__isset.key = true;
    key.coalesce();
    Keys::update(getRange(key));
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

 private:
  TypedThriftRequest(const TypedThriftRequest& other) = default;

  template <class TMList, class Derived, class... Args>
  friend class ThriftMsgDispatcher;
};

}}  // facebook::memcache
