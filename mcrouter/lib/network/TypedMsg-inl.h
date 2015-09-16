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

namespace facebook { namespace memcache { namespace detail {

template <class Proc, class... Args>
using DispatchFunc = void (*)(Proc&, Args...);

/* Function pointer for Proc::processMsg<M> */
template <class M, class Proc, class... Args>
struct DispatchImpl {
  static constexpr DispatchFunc<Proc, Args...> func =
    &Proc::template processMsg<M>;
};

/* If M is void, use nullptr function pointer */
template <class Proc, class... Args>
struct DispatchImpl<void, Proc, Args...> {
  static constexpr DispatchFunc<Proc, Args...> func = nullptr;
};

template <class TMList, class Proc, class... Args>
struct CallDispatcherImplExpanded;

/* Contains a single array that maps Ids to processMsg calls */
template <int... Ids, class... Ms, class Proc, class... Args>
struct CallDispatcherImplExpanded<List<TypedMsg<Ids, Ms>...>, Proc, Args...> {
  static constexpr
  DispatchFunc<Proc, Args...> array_[Max<Ids...>::value + 1] = {
    DispatchImpl<Ms, Proc, Args...>::func...
  };
};

/* Array needs definition outside of the class */
template <int... Ids, class... Ms, class Proc, class... Args>
constexpr DispatchFunc<Proc, Args...>
CallDispatcherImplExpanded<List<TypedMsg<Ids, Ms>...>, Proc, Args...>
::array_[Max<Ids...>::value + 1];

/* Input: unique Ids >= 0.
   Sort KVList, expand to fill 0s, call ImplExpanded */
template <class KVList, class Proc, class... Args>
struct CallDispatcherImpl
    : public CallDispatcherImplExpanded<ExpandT<SortT<KVList>>, Proc, Args...> {
};

template <class T, class TMList>
struct IdFromTypeImpl;

template <class T>
struct IdFromTypeImpl<T, List<>> {
  static constexpr int value = -1;
};

template <class T, class TM, class... TMs>
struct IdFromTypeImpl<T, List<TM, TMs...>> {
  static constexpr int value = std::is_same<T, typename TM::Value>::value
                                   ? TM::Key
                                   : IdFromTypeImpl<T, List<TMs...>>::value;
};

template <class T, class PairList>
struct ReplyFromRequestTypeImpl;

template <class T>
struct ReplyFromRequestTypeImpl<T, List<>> {
  using type = void;
};

template <class T, class P, class... Ps>
struct ReplyFromRequestTypeImpl<T, List<P, Ps...>> {
  using type = typename std::conditional<
      std::is_same<T, typename P::First::Value>::value,
      typename P::Second::Value,
      typename ReplyFromRequestTypeImpl<T, List<Ps...>>::type>::type;
};
}}}  // facebook::memcache::detail
