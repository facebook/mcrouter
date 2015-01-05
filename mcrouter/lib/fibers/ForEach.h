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

namespace facebook { namespace memcache { namespace fiber {

/**
 * Schedules several tasks and blocks until all of them are completed.
 * In the process of their successfull completion given callback would be called
 * for each of them with the index of the task and the result it returned (if
 * not void).
 * If any of these n tasks throws an exception, this exception will be
 * re-thrown, but only when all tasks are complete. If several tasks throw
 * exceptions one of them will be re-thrown. Callback won't be called for
 * tasks that throw exception.
 *
 * @param first  Range of tasks to be scheduled
 * @param last
 * @param F      callback to call for each result.
 *               In case of each task returning void it should be callable
 *                  F(size_t id)
 *               otherwise should be callable
 *                  F(size_t id, Result)
 */
template <class InputIterator, class F>
inline void forEach(InputIterator first, InputIterator last, F&& f);

}}}  // facebook::memcache::fiber

#include "mcrouter/lib/fibers/ForEach-inl.h"
