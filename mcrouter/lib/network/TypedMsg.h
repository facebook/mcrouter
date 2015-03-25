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

#include <cstddef>
#include <utility>

#include "mcrouter/lib/fbi/cpp/TypeList.h"

namespace facebook { namespace memcache {

/**
 * @param Id  non-negative message type ID
 * @param M   arbitrary type representing the message
 */
template <int Id, class M>
using TypedMsg = KV<Id, M>;

namespace detail {
template <class TMList, class Proc, class... Args>
struct CallDispatcherImpl;
}  // detail

template <class TMList>
struct StaticChecker;

template <int... Ids, class... Ms>
struct StaticChecker<List<TypedMsg<Ids, Ms>...>> {
  static_assert(Min<Ids...>::value >= 0, "Ids must be >= 0");
  static_assert(DistinctInt<Ids...>::value, "Ids must be distinct");
  static_assert(Distinct<Ms...>::value, "Types must be distinct");
};

/**
 * Call dispatcher transforms calls in the form
 *   dispatch(Id, args...)
 * to
 *   proc.processMsg<M>(args...),
 * where TypedMsg<Id, M> is an element of the specified list.
 *
 * Dispatch is done in constant time.
 *
 * @param TMList  List of supported typed messages: List<TypedMsg<Id, M>, ...>
 * @param Proc    Processor class, must provide
 *                template <class M> void processMsg()
 * @param Args    Exact argument types of processMsg() above.
 */
template <class TMList, class Proc, class... Args>
class CallDispatcher {
  StaticChecker<TMList> checker_;

 public:
  /**
   * @return true iff id is present in TMList
   */
  bool dispatch(size_t id, Proc& proc, Args... args) {
    auto& f = impl_.array_[id];
    if (f == nullptr) {
      return false;
    }
    f(proc, std::forward<Args>(args)...);
    return true;
  }

 private:
  detail::CallDispatcherImpl<TMList, Proc, Args...> impl_;
};

}}  // facebook::memcache

#include "TypedMsg-inl.h"
