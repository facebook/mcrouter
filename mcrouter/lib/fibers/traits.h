/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_CPP_TRAITS_H
#define FBI_CPP_TRAITS_H

#include <boost/type_traits.hpp>

namespace facebook { namespace memcache {

/**
 * For any functor F taking >= 1 argument,
 * FirstArgOf<F>::type is the type of F's first parameter.
 *
 * Rationale: we want to declare a function func(F), where F has the
 * signature `void(X)` and func should return T<X> (T and X are some types).
 * Solution:
 *
 * template <typename F>
 * T<typename FirstArgOf<F>::type>
 * func(F&& f);
 */

namespace detail {

/**
 * If F is a pointer-to-member, will contain a typedef type
 * with the type of F's first parameter
 */
template<typename>
struct ExtractFirstMemfn;

template <typename Ret, typename T, typename First, typename... Args>
struct ExtractFirstMemfn<Ret (T::*)(First, Args...)> {
  typedef First type;
};

template <typename Ret, typename T, typename First, typename... Args>
struct ExtractFirstMemfn<Ret (T::*)(First, Args...) const> {
  typedef First type;
};

}  // detail

/** Default - use boost */
template <typename F, typename Enable = void>
struct FirstArgOf {
  typedef typename boost::function_traits<
    typename std::remove_pointer<F>::type>::arg1_type type;
};

/** Specialization for function objects */
template <typename F>
struct FirstArgOf<F, typename std::enable_if<std::is_class<F>::value>::type> {
  typedef typename detail::ExtractFirstMemfn<
    decltype(&F::operator())>::type type;
};

}}  // facebook::memcache

#endif
