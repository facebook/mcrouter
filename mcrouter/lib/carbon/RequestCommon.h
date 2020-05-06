/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <utility>

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/Ref.h"
#include "mcrouter/lib/carbon/MessageCommon.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

namespace carbon {

class RequestCommon : public MessageCommon {
 public:
#ifndef LIBMC_FBTRACE_DISABLE
  RequestCommon() = default;

  RequestCommon(const RequestCommon& other) {
    if (other.fbtraceInfo()) {
      fbtraceInfo_ =
          McFbtraceRef::moveRef(mc_fbtrace_info_deep_copy(other.fbtraceInfo()));
    }
    traceContext_ = other.traceContext_;
  }
  RequestCommon& operator=(const RequestCommon& other) {
    if (this != &other) {
      fbtraceInfo_.reset();
      if (other.fbtraceInfo()) {
        // mc_fbtrace_info_deep_copy returns new object, just keep the refcount
        // as 1
        fbtraceInfo_ = McFbtraceRef::moveRef(
            mc_fbtrace_info_deep_copy(other.fbtraceInfo()));
      }
      traceContext_ = other.traceContext_;
    }
    return *this;
  }

  RequestCommon(RequestCommon&&) = default;
  RequestCommon& operator=(RequestCommon&&) = default;
#endif

  std::pair<uint64_t, uint64_t> traceToInts() const {
    // Trace metadata consists of trace ID and node ID
    std::pair<uint64_t, uint64_t> traceMetadata{0, 0};
#ifndef LIBMC_FBTRACE_DISABLE
    if (!fbtraceInfo_.get() ||
        fbtrace_decode(fbtraceInfo_->metadata, &traceMetadata.first) != 0 ||
        fbtrace_decode(
            fbtraceInfo_->metadata + kTraceIdSize, &traceMetadata.second) !=
            0) {
      return {0, 0};
    }
#endif
    return traceMetadata;
  }

  void setTraceId(std::pair<uint64_t, uint64_t> traceId) {
#ifndef LIBMC_FBTRACE_DISABLE
    if (traceId.first == 0 || traceId.second == 0) {
      return;
    }

    auto traceInfo = McFbtraceRef::moveRef(new_mc_fbtrace_info(0));
    if (!traceInfo.get() ||
        fbtrace_encode(traceId.first, traceInfo->metadata) != 0 ||
        fbtrace_encode(traceId.second, traceInfo->metadata + kTraceIdSize) !=
            0) {
      return;
    }
    fbtraceInfo_ = std::move(traceInfo);
#endif
  }

#ifndef LIBMC_FBTRACE_DISABLE
  mc_fbtrace_info_s* fbtraceInfo() const {
    return fbtraceInfo_.get();
  }

  /**
   * Note: will not incref info, it's up to the caller.
   */
  void setFbtraceInfo(mc_fbtrace_info_s* carbonFbtraceInfo) {
    fbtraceInfo_ = McFbtraceRef::moveRef(carbonFbtraceInfo);
  }
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
  static constexpr size_t kTraceIdSize = 11;

  const folly::IOBuf* serializedBuffer_{nullptr};

#ifndef LIBMC_FBTRACE_DISABLE
  struct McFbtraceRefPolicy {
    struct Deleter {
      void operator()(mc_fbtrace_info_t* info) const {
        mc_fbtrace_info_decref(info);
      }
    };

    static mc_fbtrace_info_t* increfOrNull(mc_fbtrace_info_t* info) {
      return mc_fbtrace_info_incref(info);
    }

    static void decref(mc_fbtrace_info_t* info) {
      mc_fbtrace_info_decref(info);
    }
  };

  using McFbtraceRef =
      facebook::memcache::Ref<mc_fbtrace_info_t, McFbtraceRefPolicy>;
  McFbtraceRef fbtraceInfo_;
#endif
};

} // namespace carbon
