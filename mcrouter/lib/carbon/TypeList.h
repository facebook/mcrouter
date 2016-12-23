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

namespace carbon {

/**
 * List for holding arbitrary types
 */
template <class... Xs>
struct List {};

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

template <class List, class T>
using ListContains = typename detail::ListContainsImpl<List, T>;

} // carbon
