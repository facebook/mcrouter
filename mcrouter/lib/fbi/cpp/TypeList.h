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

#include <type_traits>

namespace facebook { namespace memcache {

/**
 * Pair of arbitrary types.
 */
template <class L, class R>
struct Pair {
  using First = L;
  using Second = R;
};

/**
 * Type list and int list manipulation routines
 */

/**
 * List holding arbitrary types
 */
template <class... Xs>
struct List {};

/**
 * Concatenates several lists into one.
 */
namespace detail {
template <class... Lists> struct ConcatenateListsImpl;
}  // detail

template <class... Lists>
struct ConcatenateLists {
  using type = typename detail::ConcatenateListsImpl<Lists...>::type;
};

template <class... Lists>
using ConcatenateListsT = typename ConcatenateLists<Lists...>::type;

/**
 * List<KV...> can be used as an {int -> T} map
 */
template <int Id, class T>
struct KV {
  static constexpr int Key = Id;
  using Value = T;
};

template <int Id, class KVList>
struct HasKey;

template <int Id>
struct HasKey<Id, List<>> {
  static constexpr bool value = false;
};

template <int Id, int K, class V, class... KVs>
struct HasKey<Id, List<KV<K, V>, KVs...>> {
  static constexpr bool value = (Id == K) || HasKey<Id, List<KVs...>>::value;
};

/**
 * (T, List<Ts...>) -> List<T, Ts...>
 */
template <class T, class L> struct Prepend;
template <class T, class L> using PrependT = typename Prepend<T, L>::type;

template <class T, class... Ts>
struct Prepend<T, List<Ts...>> {
  using type = List<T, Ts...>;
};

/**
 * Values<List<KV<Id, T>...> -> List<T,...>
 */
namespace detail {
template <class KVList>
struct ValuesImpl;

template <>
struct ValuesImpl<List<>> {
  using type = List<>;
};

template <int Id, class T, class... KVs>
struct ValuesImpl<List<KV<Id, T>, KVs...>> {
  using type = PrependT<T, typename ValuesImpl<List<KVs...>>::type>;
};
} // detail

template <class KVList>
using Values = typename detail::ValuesImpl<KVList>::type;

/**
 * Sorts a list of KVs by Id.
 * List<KV<Id, T>...> -> List<KV<Id, T>...>
 */
namespace detail {
template <class KVList, int N, class Enable = void> struct SortImpl;
}  // detail

template <class KVList> using Sort = detail::SortImpl<KVList, 0>;
template <class KVList> using SortT = typename Sort<KVList>::type;

/**
 * Given a sorted list of KVs (Ids >= 0), fills up holes in ID space
 *
 * List<KV<2, T2>, KV<4, T4>> ->
 *   List<KV<0, void>, KV<1, void>, KV<2, T2>, KV<3, void>, KV<4, T4>>
 */
namespace detail {
template <int Start, class KVList, class Enable = void>
struct ExpandImpl;
}  // detail

template <class KVList> using Expand = detail::ExpandImpl<0, KVList>;
template <class KVList> using ExpandT = typename Expand<KVList>::type;

/**
 * (F, Xs) -> F::op(X1, F::op(X2, ...))
 */
template <class F, int... Xs>
struct Fold;
template <class F, int X>
struct Fold<F, X> {
  static constexpr int value = X;
};
template <class F, int X, int... Xs>
struct Fold<F, X, Xs...> {
  static constexpr int value = F::op(X, Fold<F, Xs...>::value);
};

/**
 * max(Xs...)
 */
struct MaxOp {
  static constexpr int op(int a, int b) { return a > b ? a : b; }
};
template <int... Xs>
using Max = Fold<MaxOp, Xs...>;

/**
 * min(Xs...)
 */
struct MinOp {
  static constexpr int op(int a, int b) { return a < b ? a : b; }
};
template <int... Xs>
using Min = Fold<MinOp, Xs...>;

/**
 * (Y, Xs) -> true iff Y is in Xs
 */
template <int Y, int... Xs>
struct HasInt;
template <int Y>
struct HasInt<Y> { static constexpr bool value = false; };
template <int Y, int X, int... Xs>
struct HasInt<Y, X, Xs...> {
  static constexpr bool value = (Y == X ? true : HasInt<Y, Xs...>::value);
};

template <class Y, class... Xs>
struct Has;
template <class Y>
struct Has<Y> { static constexpr bool value = false; };
template <class Y, class X, class... Xs>
struct Has<Y, X, Xs...> {
  static constexpr bool value =
    (std::is_same<Y, X>::value ? true : Has<Y, Xs...>::value);
};

/**
 * Xs -> true iff all Xs are distinct
 */
template <int... Xs>
struct DistinctInt;
template <int X>
struct DistinctInt<X> { static constexpr bool value = true; };
template <int X, int... Xs>
struct DistinctInt<X, Xs...> {
  static constexpr bool value =
    (HasInt<X, Xs...>::value ? false : DistinctInt<Xs...>::value);
};

template <class... Xs>
struct Distinct;
template <class X>
struct Distinct<X> { static constexpr bool value = true; };
template <class X, class... Xs>
struct Distinct<X, Xs...> {
  static constexpr bool value =
    (Has<X, Xs...>::value ? false : Distinct<Xs...>::value);
};

/**
 * Utilities for working with lists of pairs.
 */

/**
 * List<Pair<X, Y>...> -> List<X...>
 */
template <class List>
struct PairListFirst;

template <class... First, class... Second>
struct PairListFirst<List<Pair<First, Second>...>> {
  using type = List<First...>;
};

template <class List>
using PairListFirstT = typename PairListFirst<List>::type;

/**
 * List<Pair<X, Y>...> -> List<Y...>
 */
template <class List>
struct PairListSecond;

template <class... First, class... Second>
struct PairListSecond<List<Pair<First, Second>...>> {
  using type = List<Second...>;
};

template <class List>
using PairListSecondT = typename PairListSecond<List>::type;

/**
 * ListContains<L, T>::value == true if and only if T appears in L
 */
namespace detail {

template <class List, class T>
struct ListContainsImpl {
  static constexpr bool value = false;
};

template <class T>
struct ListContainsImpl<List<>, T> {
  static constexpr bool value = false;
};

template <class T, class X, class... Xs>
struct ListContainsImpl<List<X, Xs...>, T> {
  static constexpr bool value =
    std::is_same<T, X>::value ||
    ListContainsImpl<List<Xs...>, T>::value;
};

} // detail

template <class L, class T>
using ListContains = typename detail::ListContainsImpl<L, T>;

/**
 * Map a template template over a List of types.
 */
namespace detail {
template <template<typename> class F, class List>
struct MapTImpl;

template <template<typename> class F>
struct MapTImpl<F, List<>> {
  using type = List<>;
};

template <template<typename> class F, typename X, typename... Xs>
struct MapTImpl<F, List<X, Xs...>> {
  using type = PrependT<F<X>, typename MapTImpl<F, List<Xs...>>::type>;
};
} // detail

template <template<typename> class F, typename List>
using MapT = typename detail::MapTImpl<F, List>::type;

}} // facebook::memcache

#include "TypeList-inl.h"
