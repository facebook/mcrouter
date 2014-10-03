/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <boost/context/all.hpp>
#include <boost/version.hpp>

/**
 * Wrappers for different versions of boost::context library
 * API reference for different versions
 * Boost 1.51: http://www.boost.org/doc/libs/1_51_0/libs/context/doc/html/context/context/boost_fcontext.html
 * Boost 1.52: http://www.boost.org/doc/libs/1_52_0/libs/context/doc/html/context/context/boost_fcontext.html
 * Boost 1.56: http://www.boost.org/doc/libs/1_56_0/libs/context/doc/html/context/context/boost_fcontext.html
 */

namespace facebook { namespace memcache {

struct FContext {
 public:
  void* stackLimit() const {
    return stackLimit_;
  }

  void* stackBase() const {
    return stackBase_;
  }

 private:
  void* stackLimit_;
  void* stackBase_;

#if BOOST_VERSION >= 105600
  boost::context::fcontext_t context_;
#elif BOOST_VERSION >= 105200
  boost::context::fcontext_t* context_;
#else
  boost::ctx::fcontext_t context_;
#endif

  friend intptr_t jumpContext(FContext* oldC, FContext* newC, intptr_t p);
  friend FContext makeContext(void* stackLimit, size_t stackSize,
                              void(*fn)(intptr_t));
};

inline intptr_t jumpContext(FContext* oldC, FContext* newC, intptr_t p) {

#if BOOST_VERSION >= 105600
  return boost::context::jump_fcontext(&oldC->context_, newC->context_, p);
#elif BOOST_VERSION >= 105200
  return boost::context::jump_fcontext(oldC->context_, newC->context_, p);
#else
  return jump_fcontext(&oldC->context_, &newC->context_, p);
#endif

}

inline FContext makeContext(void* stackLimit, size_t stackSize,
                            void(*fn)(intptr_t)) {
  FContext res;
  res.stackLimit_ = stackLimit;
  res.stackBase_ = static_cast<unsigned char*>(stackLimit) + stackSize;

#if BOOST_VERSION >= 105200
  res.context_ = boost::context::make_fcontext(res.stackBase_, stackSize, fn);
#else
  res.context_.fc_stack.limit = stackLimit;
  res.context_.fc_stack.base = res.stackBase_;
  make_fcontext(&res.context_, fn);
#endif

  return res;
}

}}  // facebook::memcache
