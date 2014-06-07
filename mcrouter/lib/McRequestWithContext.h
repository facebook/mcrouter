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
 * A McRequest with a context value.
 * A group of Requests together have a shared ownership of a provided
 * Ctx object, which will also be shared with cloned requests and
 * preserved across move constuctor calls. Users can implement different
 * context types to share state or perform cleanup tasks.
 */
template <class Ctx>
class McRequestWithContext : public McRequestBase {
 public:
  /* Request interface */

  McRequestWithContext(McRequestWithContext&& other) noexcept = default;
  McRequestWithContext& operator=(McRequestWithContext&& other) = default;
  McRequestWithContext<Ctx> clone() const;

  McRequestWithContext(std::shared_ptr<Ctx> ctx, McMsgRef&& msg);
  McRequestWithContext(std::shared_ptr<Ctx> ctx, const std::string& key);

  /**
   * Accesses the context.
   */
  typename std::add_lvalue_reference<Ctx>::type
  context() const;

  /**
   * Accesses the shared context pointer
   */
  std::shared_ptr<Ctx> contextPtr() const;

 private:
  static_assert(!std::is_same<Ctx, void>::value,
                "Ctx should not be void. In this case you can use McRequest");
  std::shared_ptr<Ctx> ctx_;
  McRequestWithContext(const McRequestWithContext& other);
};

/**
 * Create empty mutable request
 *
 * @return  empty mutable request with param ctx as context for param Operation.
 */
template<class Operation, class Ctx>
McRequestWithContext<Ctx>
createEmptyRequest(Operation, const McRequestWithContext<Ctx>& req) {
  return McRequestWithContext<Ctx>(req.contextPtr(), createMcMsgRef());
}

}}  // facebook::memcache

#include "McRequestWithContext-inl.h"
