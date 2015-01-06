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

#include "mcrouter/lib/fbi/cpp/TypeList.h"

namespace facebook { namespace memcache {

template <class MessagesList>
class MessageStorage;

template <class... Messages>
class MessageStorage<List<Messages...>> {
 public:
  MessageStorage() = default;

  /* Type tag so that this doesn't take over move/copy constructors */
  enum StoreT { Store };
  template <class T>
  MessageStorage(StoreT, T&& t) {
    reset(std::forward<T>(t));
  }

  MessageStorage(const MessageStorage&) = delete;
  MessageStorage(MessageStorage&&) = delete;
  MessageStorage& operator=(const MessageStorage&) = delete;
  MessageStorage& operator=(MessageStorage&&) = delete;

  ~MessageStorage() noexcept {
    // Do proper cleanup of the storage.
    if (cleanupFun) {
      (this->*cleanupFun)();
    }
  }

  /**
   * Destroy any existing stored message and construct the new one with
   * provided arguments.
   *
   * @param args  arguments that will be passed to the constructor of T
   */
  template <class T, class... Args>
  void emplace(Args&&... args) {
    static_assert(Has<T, Messages...>::value,
                  "Wrong type of message used with MessageStorage!");
    // Cleanup previous value if we have one.
    if (cleanupFun) {
      (this->*cleanupFun)();
    }

    // Perform proper setup.
    new (&storage_) T(std::forward<Args>(args)...);
    type_ = typeid(T);
    cleanupFun = &MessageStorage::cleanup<T>;
  }

  /**
   * Destroy any existing stored message and construct the new one.
   *
   * @param t  new message that will be stored.
   */
  template <class T>
  void reset(T&& t) {
    emplace<T>(std::forward<T>(t));
  }

  /**
   * Returns a reference to an object of the type.
   * It's up to the user to make sure the stored type is correct.
   */
  template <class T>
  T& get() noexcept {
    static_assert(Has<T, Messages...>::value,
                  "Attempt to access incompatible message type in "
                  "MessageStorage!");
    assert(type_ == typeid(T) && cleanupFun != nullptr);
    return reinterpret_cast<T&>(storage_);
  }

  template <class T>
  const T& get() const noexcept {
    static_assert(Has<T, Messages...>::value,
                  "Attempt to access incompatible message type in "
                  "MessageStorage!");
    assert(type_ == typeid(T) && cleanupFun != nullptr);
    return reinterpret_cast<const T&>(storage_);
  }

  /**
   * Return the type of a message currently stored in this object.
   * If there's no message stored, will return typeid(void).
   */
  std::type_index contains() const noexcept {
    return type_;
  }
 private:
  static constexpr size_t kStorageSize =
    Fold<MaxOp, sizeof(Messages)...>::value;
  typename std::aligned_storage<kStorageSize>::type storage_;

  std::type_index type_{typeid(void)};
  void (MessageStorage::*cleanupFun)() noexcept {nullptr};

  template<class T>
  void cleanup() noexcept {
    reinterpret_cast<T&>(storage_).~T();
    type_ = typeid(void);
    cleanupFun = nullptr;
  }
};

}}  // facebook::memcache
