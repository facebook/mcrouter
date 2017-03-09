/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <type_traits>

namespace carbon {

template <class T>
struct ListContains<List<>, T> {
  static constexpr bool value = false;
};

template <class T, class X, class... Xs>
struct ListContains<List<X, Xs...>, T> {
  static constexpr bool value =
      std::is_same<T, X>::value || ListContains<List<Xs...>, T>::value;
};

template <class T, class... Ts>
struct Prepend<T, List<Ts...>> {
  using type = List<T, Ts...>;
};

template <>
struct ListDedup<List<>, void> {
  using type = List<>;
};

template <class X, class... Xs>
struct ListDedup<
    List<X, Xs...>,
    typename std::enable_if<!ListContains<List<Xs...>, X>::value>::type> {
  using type = typename Prepend<X, typename ListDedup<List<Xs...>>::type>::type;
};

template <class X, class... Xs>
struct ListDedup<
    List<X, Xs...>,
    typename std::enable_if<ListContains<List<Xs...>, X>::value>::type> {
  using type = typename ListDedup<List<Xs...>>::type;
};

template <class First>
struct FindByPairFirst<First, List<>> {
  using type = void;
};

template <class First, class P1, class... Ps>
struct FindByPairFirst<First, List<P1, Ps...>> {
  using type = typename std::conditional<
      std::is_same<First, typename P1::First>::value,
      typename P1::Second,
      typename FindByPairFirst<First, List<Ps...>>::type>::type;
};

} // carbon
