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

#include <type_traits>

namespace facebook { namespace memcache { namespace detail {

/* Concatenate implementation */

template <class... Items1, class... Items2>
struct ConcatenateListsImpl<List<Items1...>, List<Items2...>> {
  using type = List<Items1..., Items2...>;
};

template <class List1, class... Lists>
struct ConcatenateListsImpl<List1, Lists...> {
  using type = typename ConcatenateListsImpl<
      List1, typename ConcatenateListsImpl<Lists...>::type>::type;
};

/* Concatenate unit tests */
static_assert(
    std::is_same<List<int, double, float, long, char>,
                 ConcatenateListsT<List<int, double>, List<float, long>,
                                   List<char>>>::value,
    "concatenate is broken");

/* Sort implementation */

/* Single bubble sort iteration: for each i, order (i, i+1) */
template <class KVList, class Enable = void> struct SortIter;
template <class KVList> using SortIterT = typename SortIter<KVList>::type;

/* Case element i > element i + 1 */
template <int Idx, class Tx, int Idy, class Ty, int... Ids, class... Ts>
struct SortIter<List<KV<Idx, Tx>, KV<Idy, Ty>, KV<Ids, Ts>...>,
                typename std::enable_if<(Idx > Idy)>::type> {
  using type = PrependT<KV<Idy, Ty>,
                        SortIterT<PrependT<KV<Idx, Tx>, List<KV<Ids, Ts>...>>>>;
};

/* Case element i <= element i + 1 */
template <int Idx, class Tx, int Idy, class Ty, int... Ids, class... Ts>
struct SortIter<List<KV<Idx, Tx>, KV<Idy, Ty>, KV<Ids, Ts>...>,
                typename std::enable_if<(Idx <= Idy)>::type> {
  using type = PrependT<KV<Idx, Tx>,
                        SortIterT<PrependT<KV<Idy, Ty>, List<KV<Ids, Ts>...>>>>;
};

template <int Id, class T>
struct SortIter<List<KV<Id, T>>> {
  using type = List<KV<Id, T>>;
};

/* Sort: run the iteration above N times (N = size of list) */
template <class KVList, int N>
using SortImplT = typename SortImpl<KVList, N>::type;

template <int... Ids, class... Ts, int N>
struct SortImpl<List<KV<Ids, Ts>...>, N,
                typename std::enable_if<N == sizeof...(Ids)>::type> {
  using type = List<KV<Ids, Ts>...>;
};
template <int... Ids, class... Ts, int N>
struct SortImpl<List<KV<Ids, Ts>...>, N,
                typename std::enable_if<(N < sizeof...(Ids))>::type> {
  using type = SortImplT<SortIterT<List<KV<Ids, Ts>...>>, N + 1>;
};

/* Sort unit test */
static_assert(
  std::is_same<
  List<KV<0, int>, KV<1, char>, KV<2, int>, KV<3, double>,
       KV<4, int>, KV<5, char>, KV<6, int>, KV<7, double>>,
  SortT<List<KV<4, int>, KV<1, char>, KV<6, int>, KV<2, int>,
             KV<3, double>, KV<7, double>, KV<5, char>, KV<0, int>>>
  >::value, "sort is broken");


/* Expand implementation */
template <int Start, class KVList>
using ExpandImplT = typename ExpandImpl<Start, KVList>::type;

/* Case start == first element in the list */
template <int Start, int... Ids, class T, class... Ts>
struct ExpandImpl<Start, List<KV<Start, T>, KV<Ids, Ts>...>> {
  using type =
    PrependT<KV<Start, T>, ExpandImplT<Start + 1, List<KV<Ids, Ts>...>>>;
};

/* Case start < first element in the list, insert
   a fake element KV<start, void> */
template <int Start, int Id, int... Ids, class T, class... Ts>
struct ExpandImpl<Start, List<KV<Id, T>, KV<Ids, Ts>...>,
              typename std::enable_if<(Start < Id)>::type> {
  using type =
    PrependT<KV<Start, void>,
             ExpandImplT<Start + 1, List<KV<Id, T>, KV<Ids, Ts>...>>>;
};
template <int Start>
struct ExpandImpl<Start, List<>> {
  using type = List<>;
};

/* PairListFirst test */
static_assert(
    std::is_same<PairListFirstT<List<Pair<int, double>, Pair<float, char>>>,
                 List<int, float>>::value,
    "PairListFirst list is broken");

/* PairListSecond test */
static_assert(
    std::is_same<PairListSecondT<List<Pair<int, double>, Pair<float, char>>>,
                 List<double, char>>::value,
    "PairListSecond list is broken");
}}}  // facebook::memcaceh::detail
