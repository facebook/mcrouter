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
 * Schedules several tasks and blocks until n of these tasks are completed.
 * If any of these n tasks throws an exception, this exception will be
 * re-thrown, but only when n tasks are complete. If several tasks throw
 * exceptions one of them will be re-thrown.
 *
 * @param first Range of tasks to be scheduled
 * @param last
 * @param n Number of tasks to wait for
 *
 * @return vector of pairs (task index, return value of task)
 */
template <class InputIterator>
typename std::vector<
  typename std::enable_if<
    !std::is_same<
      typename std::result_of<
        typename std::iterator_traits<InputIterator>::value_type()>::type,
      void>::value,
    typename std::pair<
      size_t,
      typename std::result_of<
        typename std::iterator_traits<InputIterator>::value_type()>::type>
    >::type
  >
inline whenN(InputIterator first, InputIterator last, size_t n);

/**
 * whenN specialization for functions returning void
 *
 * @param first Range of tasks to be scheduled
 * @param last
 * @param n Number of tasks to wait for
 *
 * @return vector of completed task indices
 */
template <class InputIterator>
typename std::enable_if<
  std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value, std::vector<size_t>>::type
inline whenN(InputIterator first, InputIterator last, size_t n);

/**
 * Schedules several tasks and blocks until all of these tasks are completed.
 * If any of the tasks throws an exception, this exception will be re-thrown,
 * but only when all the tasks are complete. If several tasks throw exceptions
 * one of them will be re-thrown.
 *
 * @param first Range of tasks to be scheduled
 * @param last
 *
 * @return vector of values returned by tasks
 */
template <class InputIterator>
typename std::vector<
  typename std::enable_if<
    !std::is_same<
      typename std::result_of<
        typename std::iterator_traits<InputIterator>::value_type()>::type,
      void>::value,
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type>::type
  >
inline whenAll(InputIterator first, InputIterator last);

/**
 * whenAll specialization for functions returning void
 *
 * @param first Range of tasks to be scheduled
 * @param last
 */
template <class InputIterator>
typename std::enable_if<
  std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value, void>::type
inline whenAll(InputIterator first, InputIterator last);

/**
 * Schedules several tasks and blocks until one of them is completed.
 * If this task throws an exception, this exception will be re-thrown.
 * Exceptions thrown by all other tasks will be ignored.
 *
 * @param first Range of tasks to be scheduled
 * @param last
 *
 * @return pair of index of the first completed task and its return value
 */
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
inline whenAny(InputIterator first, InputIterator last);

/**
 * WhenAny specialization for functions returning void.
 *
 * @param first Range of tasks to be scheduled
 * @param last
 *
 * @return index of the first completed task
 */
template <class InputIterator>
typename std::enable_if<
  std::is_same<
    typename std::result_of<
      typename std::iterator_traits<InputIterator>::value_type()>::type, void
    >::value, size_t>::type
inline whenAny(InputIterator first, InputIterator last);

}}}

#include "mcrouter/lib/fibers/WhenN-inl.h"
