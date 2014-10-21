/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/McRequestBase.h"

namespace facebook { namespace memcache {

/**
 * The basic McRequest with clone.
 */
class McRequest : public McRequestBase {
 public:
  McRequest(McRequest&& other) noexcept = default;
  McRequest& operator=(McRequest&& other) = default;
  template<typename... Args>
  explicit McRequest(Args&&... args)
    : McRequestBase(std::forward<Args>(args)...) {}
  McRequest clone() const {
    return McRequest(*this);
  }
  static McRequest cloneFrom(const McRequestBase& other,
                             bool stripRoutingPrefix = false) {
    McRequest result(other);
    if (stripRoutingPrefix) {
      result.stripRoutingPrefix();
    }
    return result;
  }

 private:
  McRequest(const McRequest& other)
    : McRequestBase(other) {}
};


/**
 * Create empty mutable request
 *
 * @return  empty mutable request with param ctx as context for param Operation.
 */
template<class Operation>
McRequest
createEmptyRequest(Operation, const McRequest& req) {
  return McRequest(createMcMsgRef());
}

}}  // facebook::memcache
