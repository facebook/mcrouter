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

#include <queue>

#include <folly/Optional.h>
#include <folly/wangle/futures/Try.h>

#include "mcrouter/lib/fibers/FiberPromise.h"

namespace facebook { namespace memcache { namespace fiber {

template <typename T>
class TaskIterator;

/**
 * Schedules several tasks and immediately returns an iterator, that
 * allow to traverse tasks in the order of their completion. All results and
 * exptions thrown are stored alongside with the task id and are
 * accessible via iterator.
 *
 * @param first Range of tasks to be scheduled
 * @param last
 *
 * @return movable, non-copyable iterator
 */
template <class InputIterator>
TaskIterator<
  typename std::result_of<
    typename std::iterator_traits<InputIterator>::value_type()>::type>
inline addTasks(InputIterator first, InputIterator last);

template <typename T>
class TaskIterator {
 public:
  typedef T value_type;

  // not copyable
  TaskIterator(const TaskIterator& other) = delete;
  TaskIterator& operator=(const TaskIterator& other) = delete;

  // movable
  TaskIterator(TaskIterator&& other);
  TaskIterator& operator=(TaskIterator&& other) = delete;

  bool hasNext() const;

  /**
   * Await for another task to complete. Will not await if the result is
   * already available.
   *
   * @return result of the task completed.
   * @throw exception thrown by the task.
   */
  T awaitNext();

  /**
   * @return id of the last task that was processed by awaitNext().
   */
  size_t getTaskID() const;

 private:
  template <class InputIterator>
  friend TaskIterator<
   typename std::result_of<
     typename std::iterator_traits<InputIterator>::value_type()>::type>
  addTasks(InputIterator first, InputIterator last);

  struct Context {
    std::queue<std::pair<size_t, folly::wangle::Try<T>>> results;
    folly::Optional<FiberPromise<void>> promise;
  };

  std::shared_ptr<Context> context_;
  size_t id_;

  explicit TaskIterator(std::shared_ptr<Context> context);

  folly::wangle::Try<T> awaitNextResult();
};

}}}

#include "mcrouter/lib/fibers/AddTasks-inl.h"
