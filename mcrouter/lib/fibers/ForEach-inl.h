/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache { namespace fiber {

namespace {

template <class F, class G>
typename std::enable_if<
  !std::is_same<typename std::result_of<F()>::type, void>::value, void>::type
inline callFuncs(F&& f, G&& g, size_t id) {
  g(id, f());
}

template <class F, class G>
typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value, void>::type
inline callFuncs(F&& f, G&& g, size_t id) {
  f();
  g(id);
}

}  // anonymous namespace

template <class InputIterator, class F>
inline void forEach(InputIterator first, InputIterator last, F&& f) {
  if (first == last) {
    return;
  }

  typedef typename std::iterator_traits<InputIterator>::value_type FuncType;

  size_t tasksTodo = 1;
  std::exception_ptr e;
  Baton baton;

  auto taskFunc =
    [&tasksTodo, &e, &f, &baton] (size_t id, folly::MoveWrapper<FuncType> fm) {
      return [id, &tasksTodo, &e, &f, &baton, fm]() {
        try {
          callFuncs(*fm, f, id);
        } catch (...) {
          e = std::current_exception();
        }
        if (--tasksTodo == 0) {
          baton.post();
        }
      };
    };

  folly::MoveWrapper<FuncType> firstTask(std::move(*first));
  ++first;

  for (size_t i = 1; first != last; ++i, ++first, ++tasksTodo) {
    fiber::addTask(taskFunc(i, folly::makeMoveWrapper(std::move(*first))));
  }

  taskFunc(0, firstTask)();
  baton.wait();

  if (e != std::exception_ptr()) {
    std::rethrow_exception(e);
  }
}

}}}  // facebook::memcache::fiber
