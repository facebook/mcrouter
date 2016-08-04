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

#include <memory>

namespace facebook {
namespace memcache {
struct AccessPoint;
}
}

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

} // carbon
