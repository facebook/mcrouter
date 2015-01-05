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

#include <memory>

#include <folly/Range.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache {

/**
 * McMsgRef is a reference counting wrapper around mc_msg_t*
 * with value semantics.  Think of it as a shared_ptr for mc_msg_t objects.
 *
 * Copy ctor/assignment is disabled.
 * Instead, copies must be obtained explicitly:
 *   McMsgRef a;
 *   McMsgRef b = a.clone();
 * This is done to emphasize creation of new references in code and force
 * move semantics as the default.
 */
template <class T, class RefPolicy>
class Ref {
 public:

  /**
   * Construct an empty Ref.
   */
  Ref() : ref_(nullptr) {}

  /**
   * Moves in the provided pointer (no reference count changes).
   */
  static Ref moveRef(T* ref) {
    return Ref(ref);
  }

  /**
   * Clones the reference (bumps the reference count)
   */
  static Ref cloneRef(T* ref) {
    return Ref(RefPolicy::increfOrNull(ref));
  }

  Ref(Ref&& from) noexcept
  : ref_(from.ref_) {
    from.ref_ = nullptr;
  }

  template <typename M, typename D>
  /* implicit */ Ref(std::unique_ptr<M, D>&& from) : ref_(from.release()) {
    static_assert(std::is_same<D, typename RefPolicy::Deleter>::value,
                  "unique_ptr deleter is not compatible with RefPolicy");
  }

  Ref& operator=(Ref&& from) {
    if (this != &from) {
      RefPolicy::decref(ref_);
      ref_ = from.ref_;
      from.ref_ = nullptr;
    }
    return *this;
  }

  template <typename M, typename D>
  Ref& operator=(std::unique_ptr<M, D>&& from) {
    static_assert(std::is_same<D, typename RefPolicy::Deleter>::value,
                  "unique_ptr deleter is not compatible with RefPolicy");

    ref_ = from.release();
    return *this;
  }

  /**
   * Explicitly obtains a new reference to the managed object.
   */
  Ref clone() const {
    return Ref(RefPolicy::increfOrNull(ref_));
  }

  Ref(const Ref& other) = delete;
  Ref& operator=(const Ref& other) = delete;

  /**
   * Access to the managed object
   */
  T* operator->() const { return ref_; }
  T* get() const { return ref_; }
  T& operator*() const { return *ref_; }

  /**
   * Releases the managed object
   *
   * @return pointer to the managed object; the caller
   *         is responsible for managing reference count
   *         after the call.
   */
  T* release() {
    auto t = ref_;
    ref_ = nullptr;
    return t;
  }

  ~Ref() noexcept {
    RefPolicy::decref(ref_);
  }

 private:
  T* ref_;

  explicit Ref(T* ref) : ref_(ref) {}
};


struct McMsgRefPolicy {
  class Deleter {
   public:
    void operator()(mc_msg_t* msg) const {
      if (msg) {
        mc_msg_decref(msg);
      }
    }
  };

  static const mc_msg_t* increfOrNull(const mc_msg_t* msg) {
    if (msg != nullptr) {
      mc_msg_incref(const_cast<mc_msg_t*>(msg));
    }
    return msg;
  }

  static void decref(const mc_msg_t* msg) {
    if (msg != nullptr) {
      mc_msg_decref(const_cast<mc_msg_t*>(msg));
    }
  }
};

typedef Ref<const mc_msg_t, McMsgRefPolicy> McMsgRef;
typedef std::unique_ptr<mc_msg_t, McMsgRefPolicy::Deleter> MutableMcMsgRef;

/**
 * Mutable message constuctor.
 *
 * @param extra_size
 *
 * @return new Message object with empty key and value and extra_size bytes
 *         allocated after it
 */
inline MutableMcMsgRef createMcMsgRef(size_t extra_size = 0) {
  auto msg = mc_msg_new(extra_size);
  if (!msg) {
    throw std::bad_alloc();
  }
  return MutableMcMsgRef(msg);
}

/**
 * Mutable message constuctor.
 *
 * @param key
 *
 * @return new Message object with a copy of key and empty value
 *         embedded in it
 */
inline MutableMcMsgRef createMcMsgRef(const folly::StringPiece& key) {
  auto msg = mc_msg_new_with_key_full(key.data(), key.size());
  if (!msg) {
    throw std::bad_alloc();
  }
  return MutableMcMsgRef(msg);
}

/**
 * Mutable message constuctor.
 *
 * @param key
 * @param value
 *
 * @return new Message object with a copy of key and value
 *         embedded in it
 */
inline MutableMcMsgRef createMcMsgRef(const folly::StringPiece& key,
                                      const folly::StringPiece& value) {
  auto msg = mc_msg_new_with_key_and_value_full(key.data(), key.size(),
                                                value.data(), value.size());
  if (!msg) {
    throw std::bad_alloc();
  }
  return MutableMcMsgRef(msg);
}

inline MutableMcMsgRef dependentMcMsgRef(const McMsgRef& ref) {
  auto copy = createMcMsgRef();
  mc_msg_shallow_copy(copy.get(), ref.get());
  return copy;
}

/**
 * Mutable message constuctor.
 *
 * @param ref
 * @param key
 * @param value
 *
 * @return a copy of ref Message object with key and value
 *         overridden in it
 */
inline MutableMcMsgRef createMcMsgRef(const McMsgRef& ref,
                                      folly::StringPiece key,
                                      folly::StringPiece value) {

  auto msg = dependentMcMsgRef(ref);
  msg->key = to<nstring_t>(key);
  msg->value = to<nstring_t>(value);
  return msg;
}

}}  // facebook::memcache
