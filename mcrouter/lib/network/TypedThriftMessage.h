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

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/Keys.h"
#include "mcrouter/lib/network/detail/RequestUtil.h"
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
 public:
  mc_res_t result() const {
    return this->get_result();
  }
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
