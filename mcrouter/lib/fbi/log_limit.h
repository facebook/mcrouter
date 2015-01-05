/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_LOG_LIMIT_H
#define FBI_LOG_LIMIT_H

#include <sys/time.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

/*
 * set up a max number of logs for any given cycle
 *
 * @param cycle_sec   The number of seconds for a cycle
 * @param max_log_num The max number of logs for a cycle
 * @return 0 means the limit was set up successfully
 */
int set_log_limit(int max_log_num, int cycle_sec);

/*
 * check if log limit was hit
 *
 * @param now Pointer to the timestamp of now
 * @return 1 if hit, 0 if not
 */
int check_log_limit(const struct timeval *now);

__END_DECLS

#endif
