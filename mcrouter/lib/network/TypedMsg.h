/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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

namespace facebook {
namespace memcache {

namespace detail {
template <class MessageList, class Proc, class... Args>
struct CallDispatcherImpl;

template <class T, class PairList>
struct RequestFromReplyTypeImpl;
} // detail

template <class MessageList>
struct StaticChecker;

template <class... Ms>
struct StaticChecker<List<Ms...>> {
  static_assert(DistinctInt<Ms::typeId...>::value, "Type IDs must be distinct");
  static_assert(Min<Ms::typeId...>::value >= 0, "Type IDs must be nonnegative");
};

/**
 * Given a reply type T and a list of request-reply pairs, gets the request
 * type paired with T.
 */
template <class T, class PairList>
using RequestFromReplyType =
    typename detail::RequestFromReplyTypeImpl<T, PairList>::type;

/**
 * Call dispatcher transforms calls in the form
 *   dispatch(Id, args...)
 * to
 *   proc.processMsg<M>(args...),
 * where M is an element of MessageList.
 *
 * Dispatch is done in constant time.
 *
 * @param MessageList  List of supported typed messages: List<M ...>. Each M
 *                     should have a nested static member `typeId` of type
 *                     size_t.
 * @param Proc         Processor class, must provide
 *                     template <class M> void processMsg()
 * @param Args         Exact argument types of processMsg() above.
 */
template <class MessageList, class Proc, class... Args>
class CallDispatcher {
  StaticChecker<MessageList> checker_;

 public:
  /**
   * @return true iff id is the typeId of a message in MessageList
   */
  bool dispatch(size_t id, Proc& proc, Args... args) {
    if (id >= impl_.array_.size()) {
      return false;
    }
    auto& f = impl_.array_[id];
    if (f == nullptr) {
      return false;
    }
    f(proc, std::forward<Args>(args)...);
    return true;
  }

 private:
  detail::CallDispatcherImpl<MessageList, Proc, Args...> impl_;
};
}
} // facebook::memcache

#include "TypedMsg-inl.h"
