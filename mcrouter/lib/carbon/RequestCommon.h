/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <utility>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/carbon/MessageCommon.h"

namespace carbon {

class RequestCommon : public MessageCommon {
 public:
#ifndef LIBMC_FBTRACE_DISABLE
  RequestCommon() = default;

  RequestCommon(const RequestCommon& other) {
    traceContext_ = other.traceContext_;
  }
  RequestCommon& operator=(const RequestCommon& other) {
    if (this != &other) {
      traceContext_ = other.traceContext_;
    }
    return *this;
  }

  RequestCommon(RequestCommon&&) = default;
  RequestCommon& operator=(RequestCommon&&) = default;
#endif

  /**
   * Tells whether or not "serializedBuffer()" is dirty, in which case it can't
   * be used.
   */
  bool isBufferDirty() const {
    return serializedBuffer_ == nullptr;
  }

  /**
   * Sets a buffer that can be used to avoid reserializing the request.
   * If the request is modified *after* this method is called, the buffer will
   * be marked as dirty and will not be used (i.e. the request will be
   * re-serialized).
   *
   * NOTE: The caller is responsible for keeping the buffer alive until the
   * reply is received.
   */
  void setSerializedBuffer(const folly::IOBuf& buffer) {
    if (buffer.empty()) {
      serializedBuffer_ = nullptr;
    } else {
      serializedBuffer_ = &buffer;
    }
  }

  /**
   * Gets the buffer with this request serialized.
   * Will return nullptr if the buffer is dirty and can't be used.
   */
  const folly::IOBuf* serializedBuffer() const {
    return serializedBuffer_;
  }

 protected:
  void markBufferAsDirty() {
    serializedBuffer_ = nullptr;
  }

 private:
  const folly::IOBuf* serializedBuffer_{nullptr};
};

} // namespace carbon
