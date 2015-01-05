/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/fbi/timer.h"

static fb_timer_t* getTestTimer(int windowSize) {
  nstring_t name = NSTRING_LIT("test_timer");
  fb_timer_t *timer = fb_timer_alloc(name, windowSize, 0);
  EXPECT_TRUE(timer != nullptr);
  fb_timer_register(timer);
  return timer;
}

static void basicTimerChecks(fb_timer_t *timer,
                             bool checkRecent,
                             bool hasFullWindow) {
  EXPECT_TRUE(fb_timer_get_recent_peak(timer) > 0 ||
              (!checkRecent && fb_timer_get_recent_peak(timer) == 0));
  EXPECT_TRUE(fb_timer_get_recent_min(timer) > 0 ||
              (!checkRecent && fb_timer_get_recent_min(timer) == 0));
  EXPECT_TRUE(fb_timer_get_total_time(timer) > 0);
  EXPECT_TRUE(fb_timer_get_abs_min(timer) > 0);

  if (hasFullWindow) {
    EXPECT_TRUE(fb_timer_get_avg_peak(timer) > 0);
    EXPECT_TRUE(fb_timer_get_avg(timer) > 0);
    EXPECT_TRUE(fb_timer_get_avg_min(timer) > 0);

    EXPECT_TRUE(fb_timer_get_avg(timer) >= fb_timer_get_abs_min(timer));
  }

  EXPECT_TRUE(
    fb_timer_get_total_time(timer) >= fb_timer_get_recent_peak(timer));
  EXPECT_TRUE(fb_timer_get_total_time(timer) >= fb_timer_get_avg_peak(timer));
  EXPECT_TRUE(fb_timer_get_total_time(timer) >= fb_timer_get_avg(timer));
  EXPECT_TRUE(
    fb_timer_get_total_time(timer) >= fb_timer_get_recent_min(timer));
  EXPECT_TRUE(fb_timer_get_total_time(timer) >= fb_timer_get_avg_min(timer));
  EXPECT_TRUE(fb_timer_get_total_time(timer) >= fb_timer_get_abs_min(timer));

  EXPECT_TRUE(
    fb_timer_get_recent_peak(timer) >= fb_timer_get_recent_min(timer));

  EXPECT_TRUE(fb_timer_get_avg_peak(timer) >= fb_timer_get_avg_min(timer));
}

TEST(timer, no_data) {
  fb_timer_t *timer = getTestTimer(0);

  EXPECT_EQ(fb_timer_get_recent_peak(timer), 0);
  EXPECT_EQ(fb_timer_get_avg_peak(timer), 0);
  EXPECT_EQ(fb_timer_get_total_time(timer), 0);
  EXPECT_EQ(fb_timer_get_avg(timer), 0);
  EXPECT_EQ(fb_timer_get_recent_min(timer), 0);
  EXPECT_EQ(fb_timer_get_avg_min(timer), 0);
  EXPECT_EQ(fb_timer_get_abs_min(timer), 0);
}

TEST(timer, one_stat) {
  fb_timer_t *timer = getTestTimer(1);

  // First window is filled
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);

  basicTimerChecks(timer, /* checkRecent */ false, /* hasFullWindow */ true);

  double totTime = fb_timer_get_total_time(timer);
  EXPECT_EQ(fb_timer_get_avg_peak(timer), totTime);
  EXPECT_EQ(fb_timer_get_avg(timer), totTime);
  EXPECT_EQ(fb_timer_get_avg_min(timer), totTime);
  EXPECT_EQ(fb_timer_get_abs_min(timer), totTime);
}

TEST(timer, partial_window) {
  fb_timer_t *timer = getTestTimer(5);

  // First window is partially filled
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);

  basicTimerChecks(timer, /* checkRecent */ false, /* hasFullWindow */ false);
}

TEST(timer, recent_and_avg_peaks_and_mins) {
  fb_timer_t *timer = getTestTimer(2);

  // Fill the first window
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);
  fb_timer_start(timer);
  usleep(100);
  fb_timer_finish(timer);

  // Fill half of the second window
  fb_timer_start(timer);
  usleep(500);
  fb_timer_finish(timer);

  basicTimerChecks(timer, /* checkRecent */ true, /* hasFullWindow */ true);
  EXPECT_TRUE(fb_timer_get_recent_peak(timer) > fb_timer_get_avg_peak(timer));
  EXPECT_TRUE(fb_timer_get_recent_min(timer) > fb_timer_get_avg_min(timer));
}

TEST(timer, random_usleep) {
  fb_timer_t *timer = getTestTimer(0);

  // Take 100 samples with average sleep time of 50 microseconds
  for (int i = 0; i < 2 * 125; i++) {
    if (i % 2 == 0) {
      int microSec = rand() % 100 + 1;
      fb_timer_start(timer);
      usleep(microSec);
    } else {
      fb_timer_finish(timer);
    }
  }

  basicTimerChecks(timer, /* checkRecent */ true, /* hasFullWindow */ true);
}
