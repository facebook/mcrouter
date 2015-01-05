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

#include <folly/IntrusiveList.h>

namespace facebook { namespace memcache {

/**
 * TODO: enforce using UniqueIntrusiveListHook
 */
using UniqueIntrusiveListHook = folly::SafeIntrusiveListHook;

/**
 * Intrusive list with ownership semantics.
 *
 * All entries still in the list are deleted on destruction.
 * Only allows pushing/popping unique_ptr<T>.
 */
template<typename T, UniqueIntrusiveListHook T::* PtrToMember,
         typename TDeleter = std::default_delete<T>>
class UniqueIntrusiveList {
 public:
  using iterator =
    typename folly::CountedIntrusiveList<T, PtrToMember>::iterator;

  UniqueIntrusiveList(const UniqueIntrusiveList&) = delete;
  UniqueIntrusiveList& operator=(const UniqueIntrusiveList&) = delete;

  UniqueIntrusiveList() {}

  /**
   * Will destroy any elements still in the list.
   */
  ~UniqueIntrusiveList() {
    list_.clear_and_dispose(
      [] (T* t) {
        TDeleter()(t);
      }
    );
  }

  /**
   * Transfers ownership of t into this list, and appends it at the end.
   *
   * @param t  Must not be nullptr.
   * @return   Reference to *t for convenience.  For example,
   *           auto& t = list.pushBack(make_unique<T>());
   */
  T& pushBack(std::unique_ptr<T, TDeleter> t) {
    assert(t);
    list_.push_back(*t);
    t.release();
    return list_.back();
  }

  /**
   * Transfers ownership of the front element out of the list.
   */
  std::unique_ptr<T, TDeleter> popFront() {
    assert(!list_.empty());
    std::unique_ptr<T, TDeleter> t(&list_.front(), TDeleter());
    list_.pop_front();
    return t;
  }

  /**
   * Remove the element pointed to by `it` from the list,
   * and return the unique_ptr to it.
   */
  std::unique_ptr<T, TDeleter> extract(iterator it) {
    std::unique_ptr<T, TDeleter> t(&(*it), TDeleter());
    list_.erase(it);
    return t;
  }

  /* IntrusiveList interface */
  bool empty() const { return list_.empty(); }

  size_t size() const { return list_.size(); }

  T& front() { return list_.front(); }
  const T& front() const { return list_.front(); }

  T& back() { return list_.back(); }
  const T& back() const { return list_.back(); }

  iterator begin() { return list_.begin(); }
  iterator end() { return list_.end(); }

  iterator iterator_to(T& t) {
    return list_.iterator_to(t);
  }

 private:
  folly::CountedIntrusiveList<T, PtrToMember> list_;
};

}}  // facebook::memcache
