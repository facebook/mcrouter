/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_TEST_UTIL_H
#define FBI_TEST_UTIL_H

#include <functional>

double measure_time(std::function<void(void)> f);
double measure_time_concurrent(unsigned thread_count,
                               std::function<void(unsigned)> f);

#endif
