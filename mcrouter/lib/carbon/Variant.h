/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <type_traits>
#include <typeindex>
#include <utility>

#include "mcrouter/lib/fbi/cpp/TypeList.h"

namespace carbon {

template <class... Ts>
class Variant {
 public:
  static_assert(
      facebook::memcache::Distinct<Ts...>::value,
      "Variant may only be used with a list of pairwise distinct types");

  Variant() = default;

  Variant(const Variant&) = default;
  Variant(Variant&&) = default;

  Variant& operator=(const Variant&) = default;
  Variant& operator=(Variant&&) = default;

  template <class T>
  Variant& operator=(T&& t) {
    static_assert(
        std::is_move_constructible<T>::value,
        "Variant::operator=(T&&) requires that T be move-constructible");
    emplace<T>(std::forward<T>(t));
    return *this;
  }

  ~Variant() noexcept {
    // Do proper cleanup of the storage.
    if (cleanupFun_) {
      (this->*cleanupFun_)();
    }
  }

  /**
   * Destroy any existing stored object and construct the new one with
   * provided arguments.
   *
   * @param args  arguments that will be passed to the constructor of T
   */
  template <class T, class... Args>
  T& emplace(Args&&... args) {
    static_assert(
        facebook::memcache::Has<T, Ts...>::value,
        "Wrong type used with Variant!");
    // Cleanup previous value if we have one.
    if (cleanupFun_) {
      (this->*cleanupFun_)();
    }

    // Perform proper setup.
    new (&storage_) T(std::forward<Args>(args)...);
    type_ = typeid(T);
    cleanupFun_ = &Variant::cleanup<T>;
    return reinterpret_cast<T&>(storage_);
  }

  /**
   * Returns a reference to an object of the type.
   * It's up to the user to make sure the stored type is correct.
   */
  template <class T>
  T& get() noexcept {
    static_assert(
        facebook::memcache::Has<T, Ts...>::value,
        "Attempt to access incompatible type in Variant!");
    assert(type_ == typeid(T) && cleanupFun_ != nullptr);
    return reinterpret_cast<T&>(storage_);
  }

  template <class T>
  const T& get() const noexcept {
    static_assert(
        facebook::memcache::Has<T, Ts...>::value,
        "Attempt to access incompatible type in Variant!");
    assert(type_ == typeid(T) && cleanupFun_ != nullptr);
    return reinterpret_cast<const T&>(storage_);
  }

  /**
   * Return the type of the currently stored object.
   * If there's no object stored, will return typeid(void).
   */
  std::type_index which() const noexcept {
    return type_;
  }

 private:
  static constexpr size_t kStorageSize =
      facebook::memcache::Fold<facebook::memcache::MaxOp, sizeof(Ts)...>::value;

  typename std::aligned_storage<kStorageSize>::type storage_;
  std::type_index type_{typeid(void)};
  void (Variant::*cleanupFun_)() noexcept {nullptr};

  template <class T>
  void cleanup() noexcept {
    reinterpret_cast<T&>(storage_).~T();
    type_ = typeid(void);
    cleanupFun_ = nullptr;
  }
};

namespace detail {
template <class... Ts>
struct VariantFromList;

template <class... Ts>
struct VariantFromList<List<Ts...>> {
  using type = Variant<Ts...>;
};
} // detail

template <class TList>
using makeVariantFromList = typename detail::VariantFromList<TList>::type;

} // carbon
