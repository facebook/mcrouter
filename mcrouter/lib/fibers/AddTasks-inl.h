/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <memory>
#include <vector>

#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache { namespace fiber {

template <typename T>
TaskIterator<T>::TaskIterator(TaskIterator&& other) noexcept
    : context_(std::move(other.context_)),
      id_(other.id_) {
}

template <typename T>
TaskIterator<T>::TaskIterator(std::shared_ptr<Context> context)
    : context_(std::move(context)),
      id_(-1) {
  assert(context_);
}

template <typename T>
inline bool TaskIterator<T>::hasCompleted() const {
  return context_->tasksConsumed < context_->results.size();
}

template <typename T>
inline bool TaskIterator<T>::hasPending() const {
  return !context_.unique();
}

template <typename T>
inline bool TaskIterator<T>::hasNext() const {
  return hasPending() || hasCompleted();
}

template <typename T>
folly::Try<T> TaskIterator<T>::awaitNextResult() {
  assert(hasCompleted() || hasPending());
  reserve(1);

  size_t i = context_->tasksConsumed++;
  id_ = context_->results[i].first;
  return std::move(context_->results[i].second);
}

template <typename T>
inline T TaskIterator<T>::awaitNext() {
  return std::move(awaitNextResult().value());
}

template <>
inline void TaskIterator<void>::awaitNext() {
  awaitNextResult().value();
}

template <typename T>
inline void TaskIterator<T>::reserve(size_t n) {
  size_t tasksReady = context_->results.size() - context_->tasksConsumed;

  // we don't need to do anything if there are already n or more tasks complete
  // or if we have no tasks left to execute.
  if (!hasPending() || tasksReady >= n) {
    return;
  }

  n -= tasksReady;
  size_t tasksLeft = context_->totalTasks - context_->results.size();
  n = std::min(n, tasksLeft);

  fiber::await(
    [this, n](FiberPromise<void> promise) {
      context_->tasksToFulfillPromise = n;
      context_->promise.assign(std::move(promise));
    });
}

template <typename T>
inline size_t TaskIterator<T>::getTaskID() const {
  assert(id_ != -1);
  return id_;
}

template <class InputIterator>
TaskIterator<typename std::result_of<
  typename std::iterator_traits<InputIterator>::value_type()>::type>
addTasks(InputIterator first, InputIterator last) {
  typedef typename std::result_of<
    typename std::iterator_traits<InputIterator>::value_type()>::type
      ResultType;
  typedef TaskIterator<ResultType> IteratorType;

  auto context = std::make_shared<typename IteratorType::Context>();
  context->totalTasks = std::distance(first, last);
  context->results.reserve(context->totalTasks);

  for (size_t i = 0; first != last; ++i, ++first) {
#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
    fiber::addTask(
      [i, context, f = std::move(*first)]() {
        context->results.emplace_back(i, folly::makeTryFunction(std::move(f)));

        // Check for awaiting iterator.
        if (context->promise.hasValue()) {
          if (--context->tasksToFulfillPromise == 0) {
            context->promise->setValue();
            context->promise.clear();
          }
        }
      }
    );
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  }

  return IteratorType(std::move(context));
}

}}}
