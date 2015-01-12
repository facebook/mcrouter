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

#include <folly/MoveWrapper.h>

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
  return !context_->results.empty();
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
folly::wangle::Try<T> TaskIterator<T>::awaitNextResult() {
  assert(hasCompleted() || hasPending());
  reserve(1);

  id_ = context_->results.front().first;
  auto result = std::move(context_->results.front().second);
  context_->results.pop();

  return std::move(result);
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
  if (context_->results.size() >= n) {
    return;
  }
  n -= context_->results.size();

  n = std::min(n, context_->tasksLeft);

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

  for (size_t i = 0; first != last; ++i, ++first) {
    auto fm = folly::makeMoveWrapper(std::move(*first));
    ++context->tasksLeft;

    fiber::addTask(
      [i, context, fm]() {
        context->results.emplace(i, folly::wangle::makeTryFunction(*fm));
        --context->tasksLeft;

        // Check for awaiting iterator.
        if (context->promise.hasValue()) {
          if (--context->tasksToFulfillPromise == 0) {
            context->promise->setValue();
            context->promise.clear();
          }
        }
      }
    );
  }

  return IteratorType(std::move(context));
}

}}}
