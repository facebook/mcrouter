/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <pthread.h>
#include <unistd.h>

#include <thread>

#include <gtest/gtest.h>

#include "mcrouter/lib/fbi/cwlock.h"
#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/fbi/test/test_util.h"

const unsigned repeat = 1000000U;

TEST(cwlock, uncontended_acquire) {
  double t;
  cwlock_t cwlock;

  cwlock_init(&cwlock);

  t = measure_time([&] () {
      for (unsigned cnt = repeat; cnt; cnt--) {
        if (cwlock_lock(&cwlock)) {
          cwlock_unlock(&cwlock);
        }
      }
    });

  printf("cwlock_t time: %lf ms\n", t / 1e6);
}

TEST(cwlock, contended_single_acquirer) {
  const unsigned thread_count = 30;
  cwlock_t cwlock;
  unsigned owners = 0;

  cwlock_init(&cwlock);

  measure_time_concurrent(thread_count, [&] (unsigned) {
      for (unsigned j = repeat; j; j--) {
        if (cwlock_lock(&cwlock)) {
          EXPECT_EQ(__sync_fetch_and_add(&owners, 1), 0);
          EXPECT_EQ(__sync_sub_and_fetch(&owners, 1), 0);
          cwlock_unlock(&cwlock);
        }
      }
    });
}

TEST(cwlock, contended_release_only_once_done) {
  const unsigned thread_count = 30;
  cwlock_t cwlock;
  unsigned inside = 0;
  unsigned owners = 0;

  cwlock_init(&cwlock);

  measure_time_concurrent(thread_count, [&] (unsigned) {
      if (cwlock_lock(&cwlock)) {
        EXPECT_EQ(__sync_fetch_and_add(&owners, 1), 0);
        /*
         * Sleep for half a second so that all threads will block before we
         * increment the inside count and release the other threads.
         */
        usleep(500000);
        __sync_fetch_and_add(&inside, 1);
        EXPECT_EQ(__sync_sub_and_fetch(&owners, 1), 0);
        cwlock_unlock(&cwlock);
      }

      EXPECT_EQ(inside, 1);
    });
}
