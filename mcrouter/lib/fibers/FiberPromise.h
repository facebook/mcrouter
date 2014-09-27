/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <folly/wangle/Try.h>

namespace facebook { namespace memcache {

class Baton;
class FiberManager;

template <typename T>
class FiberPromise {
 public:
  typedef T value_type;

  ~FiberPromise();

  // not copyable
  FiberPromise(const FiberPromise&) = delete;
  FiberPromise& operator=(const FiberPromise&) = delete;

  // movable
  FiberPromise(FiberPromise&&) noexcept;
  FiberPromise& operator=(FiberPromise&&);

  /** Fulfil this promise (only for FiberPromise<void>) */
  void setValue();

  /** Set the value (use perfect forwarding for both move and copy) */
  template <class M>
  void setValue(M&& value);

  /**
   * Fulfill the promise with a given try
   *
   * @param t
   */
  void fulfilTry(folly::wangle::Try<T>&& t);

  /** Fulfil this promise with the result of a function that takes no
    arguments and returns something implicitly convertible to T.
    Captures exceptions. e.g.

    p.fulfil([] { do something that may throw; return a T; });
  */
  template <class F>
  void fulfil(F&& func);

  /** Fulfil the Promise with an exception_ptr, e.g.
    try {
      ...
    } catch (...) {
      p.setException(std::current_exception());
    }
    */
  void setException(std::exception_ptr);

 private:
  friend class FiberManager;

  FiberPromise(folly::wangle::Try<T>& value, Baton& baton);
  folly::wangle::Try<T>* value_;
  Baton* baton_;

  void throwIfFulfilled() const;

  template <class F>
  typename std::enable_if<
    std::is_convertible<typename std::result_of<F()>::type, T>::value &&
    !std::is_same<T, void>::value>::type
  fulfilHelper(F&& func);

  template <class F>
  typename std::enable_if<
    std::is_same<typename std::result_of<F()>::type, void>::value &&
    std::is_same<T, void>::value>::type
  fulfilHelper(F&& func);
};

}}

#include <mcrouter/lib/fibers/FiberPromise-inl.h>
