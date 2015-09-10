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

namespace facebook {
namespace memcache {

template <int Op>
class McOperation;
/**
 * Class for serializing requests in the form of thrift structs.
 */
class CaretSerializedMessage {
 public:
  CaretSerializedMessage() noexcept = default;

  CaretSerializedMessage(const CaretSerializedMessage&) = delete;
  CaretSerializedMessage& operator=(const CaretSerializedMessage&) = delete;
  CaretSerializedMessage(CaretSerializedMessage&&) = delete;
  CaretSerializedMessage& operator=(CaretSerializedMessage&&) = delete;

  /**
   * Message serialization not supported for now.
   */
  template <class Arg, int Op>
  bool prepare(const Arg& arg,
               McOperation<Op>,
               size_t reqId,
               struct iovec*& iovOut,
               size_t& niovOut) noexcept {
    return false;
  }

  template <class Reply>
  bool prepare(Reply&& reply,
               size_t reqId,
               size_t typeId,
               struct iovec*& iovOut,
               size_t& niovOut) noexcept {
    return false;
  }

  void clear() {}
};
}
} // facebook::memcache
