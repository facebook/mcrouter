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

#include <folly/io/IOBuf.h>
#include <folly/Range.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

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

  TypedThriftMessage() = default;

  TypedThriftMessage& operator=(const TypedThriftMessage& other) = delete;

  TypedThriftMessage(TypedThriftMessage&& other) noexcept = default;
  TypedThriftMessage& operator=(TypedThriftMessage&& other) = default;

 private:
  M raw_;

  template <class Protocol>
  uint32_t read(Protocol* iprot) {
    return raw_.read(iprot);
  }

  template <class TMList, class Derived, class... Args>
  friend class ThriftMsgDispatcher;
};

}}  // facebook::memcache
