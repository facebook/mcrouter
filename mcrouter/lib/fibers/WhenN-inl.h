/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "folly/Optional.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache { namespace fiber {

template <class InputIterator>
typename std::vector<
  typename std::enable_if<
    !std::is_same<
      typename std::result_of<
        typename std::iterator_traits<InputIterator>::value_type()>::type, void
      >::value,
    typename std::pair<
      size_t,
      typename std::result_of<
        typename std::iterator_traits<InputIterator>::value_type()>::type>
    >::type
  >
whenN(InputIterator first, InputIterator last, size_t n) {
  typedef typename std::result_of<
    typename std::iterator_traits<InputIterator>::value_type()>::type Result;
  assert(n > 0);
  assert(n <= std::distance(first, last));

  struct Context {
    std::vector<std::pair<size_t, Result>> results;
    size_t tasksTodo;
    std::exception_ptr e;
    folly::Optional<FiberPromise<void>> promise;

    Context(size_t tasksTodo_) : tasksTodo(tasksTodo_) {
      this->results.reserve(tasksTodo_);
    }
  };
  auto context = std::make_shared<Context>(n);

  fiber::await(
    [first, last, context](FiberPromise<void> promise) mutable {
      context->promise = std::move(promise);
      for (size_t i = 0; first != last; ++i, ++first) {
        folly::MoveWrapper<
          typename std::iterator_traits<InputIterator>::value_type> fm(
            std::move(*first));

        fiber::addTask(
          [i, context, fm]() {
            try {
              auto result = (*fm)();
              if (context->tasksTodo == 0) {
                return;
              }
              context->results.emplace_back(i, std::move(result));
            } catch (...) {
              if (context->tasksTodo == 0) {
                return;
              }
              context->e = std::current_exception();
            }
            if (--context->tasksTodo == 0) {
              context->promise->setValue();
            }
          });
      }
    });

  if (context->e != std::exception_ptr()) {
    std::rethrow_exception(context->e);
  }

  return std::move(context->results);
}

template <class InputIterator>
typename std::enable_if<
  std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value, std::vector<size_t>>::type
whenN(InputIterator first, InputIterator last, size_t n) {
  assert(n > 0);
  assert(n <= std::distance(first, last));

  struct Context {
    std::vector<size_t> taskIndices;
    std::exception_ptr e;
    size_t tasksTodo;
    folly::Optional<FiberPromise<void>> promise;

    Context(size_t tasksTodo_) : tasksTodo(tasksTodo_) {
      this->taskIndices.reserve(tasksTodo_);
    }
  };
  auto context = std::make_shared<Context>(n);

  fiber::await(
    [first, last, context](FiberPromise<void> promise) mutable {
      context->promise = std::move(promise);
      for (size_t i = 0; first != last; ++i, ++first) {
        folly::MoveWrapper<
          typename std::iterator_traits<InputIterator>::value_type> fm(
            std::move(*first));

        fiber::addTask(
          [i, context, fm]() {
            try {
              (*fm)();
              if (context->tasksTodo == 0) {
                return;
              }
              context->taskIndices.push_back(i);
            } catch (...) {
              if (context->tasksTodo == 0) {
                return;
              }
              context->e = std::current_exception();
            }
            if (--context->tasksTodo == 0) {
              context->promise->setValue();
            }
          });
      }
    });

  if (context->e != std::exception_ptr()) {
    std::rethrow_exception(context->e);
  }

  return context->taskIndices;
}

template <class InputIterator>
typename std::vector<
  typename std::enable_if<
    !std::is_same<
      typename std::result_of<
        typename std::iterator_traits<InputIterator>::value_type()>::type, void
      >::value,
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type>::type>
inline whenAll(InputIterator first, InputIterator last) {
  typedef typename std::result_of<
    typename std::iterator_traits<InputIterator>::value_type()>::type Result;
  size_t n = std::distance(first, last);
  auto results = whenN(first, last, n);
  assert(results.size() == n);

  std::vector<size_t> order(n);
  for (size_t i = 0; i < n; ++i) {
    order[results[i].first] = i;
  }
  std::vector<Result> ret;
  ret.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    ret.push_back(std::move(results[order[i]].second));
  }

  return ret;
}

template <class InputIterator>
typename std::enable_if<
  std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value, void>::type
inline whenAll(InputIterator first, InputIterator last) {
  whenN(first, last, std::distance(first, last));
}

template <class InputIterator>
typename std::enable_if<
  !std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value,
  typename std::pair<
    size_t,
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type>
  >::type
inline whenAny(InputIterator first, InputIterator last) {
  auto result = whenN(first, last, 1);
  assert(result.size() == 1);
  return std::move(result[0]);
}

template <class InputIterator>
typename std::enable_if<
  std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value, size_t>::type
inline whenAny(InputIterator first, InputIterator last) {
  auto result = whenN(first, last, 1);
  assert(result.size() == 1);
  return std::move(result[0]);
}

}}}
