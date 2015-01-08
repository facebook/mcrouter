/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
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
    setException(folly::make_exception_wrapper<std::logic_error>(
        "promise not fulfilled"));
  }
}

template <class T>
void FiberPromise<T>::setException(folly::exception_wrapper e) {
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
  fulfilTry(makeTryFunction(std::forward<F>(func)));
}

}}
