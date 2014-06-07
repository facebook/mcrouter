/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache {

template <typename... Operations>
struct List {};

template <typename List1, typename List2>
struct ConcatLists {};

template <typename... Operations1, typename Operation2, typename... Operations2>
struct ConcatLists<List<Operations1...>, List<Operation2, Operations2...>> {
  typedef typename ConcatLists<List<Operations1..., Operation2>,
                               List<Operations2...>>::type
    type;
};

template <typename... Operations1>
struct ConcatLists<List<Operations1...>, List<>> {
  typedef List<Operations1...> type;
};

template <typename List1, typename List2>
using ConcatListsT = typename ConcatLists<List1, List2>::type;

}}
