/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <memory>

#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {
struct AccessPoint;
} // memcache
} // facebook

namespace carbon {

class ReplyCommon {
 public:
  const std::shared_ptr<const facebook::memcache::AccessPoint>& destination()
      const noexcept {
    return destination_;
  }

  void setDestination(
      std::shared_ptr<const facebook::memcache::AccessPoint> ap) noexcept {
    destination_ = std::move(ap);
  }

 private:
  std::shared_ptr<const facebook::memcache::AccessPoint> destination_;
};

class ReplyCommonThrift : public ReplyCommon {
 public:
  explicit ReplyCommonThrift(mc_res_t result__ = mc_res_unknown)
      : result_(result__) {}

  mc_res_t result() const {
    return result_;
  }

  mc_res_t& result() {
    return result_;
  }

 private:
  mc_res_t result_{mc_res_unknown};
};

} // carbon
