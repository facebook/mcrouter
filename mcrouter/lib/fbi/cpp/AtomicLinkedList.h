/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <atomic>
#include <cassert>

namespace facebook { namespace memcache {

/**
 * A very simple atomic single-linked list primitive.
 *
 * Usage:
 *
 * class MyClass {
 *   AtomicLinkedListHook<MyClass> hook_;
 * }
 *
 * AtomicLinkedList<MyClass, &MyClass::hook_> list;
 * list.insert(&a);
 * list.sweep([] (MyClass* c) { doSomething(c); }
 */
template <class T>
struct AtomicLinkedListHook {
  T* next{nullptr};
};

template <class T, AtomicLinkedListHook<T> T::* HookMember>
class AtomicLinkedList {
 public:
  AtomicLinkedList() {}

  /**
   * Note: list must be empty on destruction.
   */
  ~AtomicLinkedList() {
    assert(head_ == nullptr);
  }

  bool empty() const {
    return head_ == nullptr;
  }

  /**
   * Atomically insert t at the head of the list.
   * @return True if the inserted element is the only one in the list
   *         after the call.
   */
  bool insertHead(T* t) {
    assert(next(t) == nullptr);

    auto oldHead = head_.load(std::memory_order_relaxed);
    do {
      next(t) = oldHead;
      /* oldHead is updated by the call below.

         NOTE: we don't use next(t) instead of oldHead directly due to
         compiler bugs (GCC prior to 4.8.3 (bug 60272), clang (bug 18899),
         MSVC (bug 819819); source:
         http://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange */
    } while (!head_.compare_exchange_weak(oldHead, t,
                                          std::memory_order_release,
                                          std::memory_order_relaxed));

    return oldHead == nullptr;
  }

  /**
   * Repeatedly replaces the head with nullptr,
   * and calls func() on the removed elements in the order from tail to head.
   * Stops when the list is empty.
   */
  template <typename F>
  void sweep(F&& func) {
    while (auto head = head_.exchange(nullptr)) {
      auto rhead = reverse(head);
      while (rhead != nullptr) {
        auto t = rhead;
        rhead = next(t);
        next(t) = nullptr;
        func(t);
      }
    }
  }

 private:
  std::atomic<T*> head_{nullptr};

  static T*& next(T* t) {
    return (t->*HookMember).next;
  }

  /* Reverses a linked list, returning the pointer to the new head
     (old tail) */
  static T* reverse(T* head) {
    T* rhead = nullptr;
    while (head != nullptr) {
      auto t = head;
      head = next(t);
      next(t) = rhead;
      rhead = t;
    }
    return rhead;
  }
};

}}  // facebook::memcache
