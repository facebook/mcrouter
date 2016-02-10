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
class TypedThriftMessage : private std::conditional<HasKey<M>::value,
                                                    Keys,
                                                    DefaultKeys>::type {
 private:
  using KeysT = typename std::conditional<HasKey<M>::value,
                                          Keys,
                                          DefaultKeys>::type;

  template <class T, bool enable>
  using WithKeyT = typename std::enable_if<HasKey<M>::value == enable, T>::type;

 public:
  using rawType = M;
  /* TODO(jmswen) Add names for Thrift types */
  static constexpr const char* name = "TypedThriftMessagePlaceholder";

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

  TypedThriftMessage() = default;

  TypedThriftMessage clone() const {
    return *this;
  }

  TypedThriftMessage& operator=(const TypedThriftMessage& other) = delete;

  TypedThriftMessage(TypedThriftMessage&& other) noexcept = default;
  TypedThriftMessage& operator=(TypedThriftMessage&& other) = default;

  template <class T = folly::StringPiece>
  WithKeyT<T, true> fullKey() const {
    return getRange(raw_.key);
  }

  template <class T = folly::StringPiece>
  WithKeyT<T, false> fullKey() const {
    return "";
  }

  template <class T = void>
  WithKeyT<T, true> setKey(folly::StringPiece k) {
    // TODO(jmswen) Update to use thrift setters, here and elsewhere
    auto& key = raw_.key;
    key = folly::IOBuf(folly::IOBuf::COPY_BUFFER, k);
    raw_.__isset.key = true;
    KeysT::update(getRange(key));
  }

  template <class T = void>
  WithKeyT<T, false> setKey(folly::StringPiece k) {
  }

  template <class T = void>
  WithKeyT<T, true> setKey(folly::IOBuf k) {
    auto& key = raw_.key;
    key = std::move(k);
    raw_.__isset.key = true;
    key.coalesce();
    KeysT::update(getRange(key));
  }

  template <class T = void>
  WithKeyT<T, false> setKey(folly::IOBuf k) {
  }

  folly::StringPiece keyWithoutRoute() const {
    return KeysT::keyWithoutRoute();
  }

  folly::StringPiece routingKey() const {
    return KeysT::routingKey();
  }

  folly::StringPiece routingPrefix() const {
    return KeysT::routingPrefix();
  }

  uint32_t routingKeyHash() const {
    return KeysT::routingKeyHash();
  }

 private:
  M raw_;

  TypedThriftMessage(const TypedThriftMessage& other) = default;

  template <class Protocol>
  uint32_t read(Protocol* iprot) {
    return raw_.read(iprot);
  }

  template <class TMList, class Derived, class... Args>
  friend class ThriftMsgDispatcher;
};

}}  // facebook::memcache
