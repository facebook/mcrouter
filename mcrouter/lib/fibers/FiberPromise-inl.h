/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/fibers/Baton.h"

namespace facebook { namespace memcache {

template <class T>
FiberPromise<T>::FiberPromise(folly::wangle::Try<T>& value, Baton& baton) :
    value_(&value), baton_(&baton)
{}

template <class T>
FiberPromise<T>::FiberPromise(FiberPromise&& other) noexcept :
value_(other.value_), baton_(other.baton_) {
  other.value_ = nullptr;
  other.baton_ = nullptr;
}

template <class T>
FiberPromise<T>& FiberPromise<T>::operator=(FiberPromise&& other) {
  std::swap(value_, other.value_);
  std::swap(baton_, other.baton_);
  return *this;
}

template <class T>
void FiberPromise<T>::throwIfFulfilled() const {
  if (!value_) {
    throw std::logic_error("promise already fulfilled");
  }
}

template <class T>
FiberPromise<T>::~FiberPromise() {
  if (value_) {
    setException(
      std::make_exception_ptr(
        std::logic_error("promise not fulfilled")));
  }
}

template <class T>
void FiberPromise<T>::setException(std::exception_ptr e) {
  fulfilTry(folly::wangle::Try<T>(e));
}

template <class T>
void FiberPromise<T>::fulfilTry(folly::wangle::Try<T>&& t) {
  throwIfFulfilled();

  *value_ = std::move(t);
  baton_->post();

  value_ = nullptr;
  baton_ = nullptr;
}

template <class T>
template <class M>
void FiberPromise<T>::setValue(M&& v) {
  static_assert(!std::is_same<T, void>::value,
                "Use setValue() instead");

  fulfilTry(folly::wangle::Try<T>(std::forward<M>(v)));
}

template <class T>
void FiberPromise<T>::setValue() {
  static_assert(std::is_same<T, void>::value,
                "Use setValue(value) instead");

  fulfilTry(folly::wangle::Try<void>());
}

template <class T>
template <class F>
void FiberPromise<T>::fulfil(F&& func) {
  fulfilHelper(func);
}

template <class T>
template <class F>
typename std::enable_if<
  std::is_convertible<typename std::result_of<F()>::type, T>::value &&
  !std::is_same<T, void>::value>::type
inline FiberPromise<T>::fulfilHelper(F&& func) {
  try {
    setValue(func());
  } catch (...) {
    setException(std::current_exception());
  }
}

template <class T>
template <class F>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value &&
  std::is_same<T, void>::value>::type
inline FiberPromise<T>::fulfilHelper(F&& func) {
  try {
    func();
    setValue();
  } catch (...) {
    setException(std::current_exception());
  }
}


}}
