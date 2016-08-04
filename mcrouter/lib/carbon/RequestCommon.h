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

#include "mcrouter/lib/Ref.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

namespace carbon {

class RequestCommon {
 public:
#ifndef LIBMC_FBTRACE_DISABLE
  RequestCommon() = default;

  RequestCommon(const RequestCommon& other) {
    if (other.fbtraceInfo()) {
      fbtraceInfo_ =
          McFbtraceRef::moveRef(mc_fbtrace_info_deep_copy(other.fbtraceInfo()));
    }
  }
  RequestCommon& operator=(const RequestCommon&) = delete;

  RequestCommon(RequestCommon&&) = default;
  RequestCommon& operator=(RequestCommon&&) = default;
#endif

  uint64_t traceId() const {
    return traceId_;
  }

  void setTraceId(uint64_t carbonTraceId) {
    traceId_ = carbonTraceId;
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

 private:
  uint64_t traceId_{0};
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

} // carbon
