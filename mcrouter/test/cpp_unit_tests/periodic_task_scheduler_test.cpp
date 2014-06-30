/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <gtest/gtest.h>

#include "mcrouter/PeriodicTaskScheduler.h"

using namespace std;

using facebook::memcache::mcrouter::PeriodicTaskScheduler;

TEST(PeriodicTaskScheduler, test_sanity) {
  PeriodicTaskScheduler obj;
  int counter1 = 0, counter2 = 0;
  std::condition_variable cv1, cv2;
  std::mutex mutex1, mutex2;

  obj.scheduleTask(1, [&counter1, &cv1](PeriodicTaskScheduler&)
                          { counter1++; cv1.notify_all(); });
  {
    std::unique_lock<std::mutex> lock(mutex1);
    cv1.wait_for(lock, std::chrono::seconds(1));
  }
  obj.scheduleTask(1, [&counter2, &cv2](PeriodicTaskScheduler&)
                          { counter2++; cv2.notify_all(); });
  {
    std::unique_lock<std::mutex> lock(mutex2);
    cv2.wait_for(lock, std::chrono::seconds(1));
  }

  EXPECT_TRUE(counter1 > 0);
  EXPECT_TRUE(counter2 > 0);
  obj.shutdownAllTasks();

  // Check that threads have stopped and values do not change any furthur.
  int currCounter1 = counter1;
  int currCounter2 = counter2;
  usleep(1500);
  EXPECT_EQ(counter1, currCounter1);
  EXPECT_EQ(counter2, currCounter2);
}
