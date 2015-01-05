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

#include <atomic>

#include <gtest/gtest.h>

#include "mcrouter/lib/fbi/counter.h"

typedef struct test_params_s {
  int num_counters;
  counter_t *counters;
  int num_threads;
  int steps;
  std::atomic<bool> done;
} test_params_t;

static void* run_test_body(void *arg) {
  const test_params_t* params = (const test_params_t *)arg;
  intptr_t s;

  for (s = 0; s < params->steps && !params->done; ++s) {
    /* collapse 50% of the steps to i = 0 */
    int i = (s & 1) ? 0 : (s >> 1) % params->num_counters;
    counter_add(params->counters + i, 1);
  }
  return (void*)s;
}

/** This runs num_threads threads, each of which performs up to steps
 *  increments across num_counters counters, with half of the increments
 *  going to the first counter.  If duration is positive, the test will be
 *  ended after that many seconds and the total number of steps will be
 *  printed.  If duration is zero then the test will verify that all of the
 *  totals are as expected, returning non-zero otherwise.
 */
static int run_test(int num_counters, int num_threads, int steps,
                    int duration) {
  counter_t counters[num_counters];
  test_params_t params = { num_counters, counters, num_threads, steps };
  params.done = false;
  pthread_t pids[num_threads];
  int i;
  intptr_t total;
  int rv = 0;

  memset(counters, 0, sizeof(counter_t) * num_counters);

  for (i = 0; i < num_threads; ++i) {
    pthread_create(pids + i, nullptr, run_test_body, &params);
  }
  if (duration > 0) {
    sleep(duration);
    params.done = true;
  }
  total = 0;
  for (i = 0; i < num_threads; ++i) {
    void* s;
    pthread_join(pids[i], &s);
    total += (int)(intptr_t)s;
  }

  if (duration == 0) {
    for (i = 0; i < num_counters; ++i) {
      int64_t expected =
        ((steps / 2 + num_counters - 1 - i) / num_counters);
      if (i == 0) {
        expected += (steps + 1) / 2;
      }

      if (counter_get(counters + i) != expected * num_threads) {
        fprintf(stderr, "counter %d had mismatch\n", i);
        rv = -1;
      }
    }
  }

  if (duration > 0) {
    printf("(%d counters, %d threads, %d seconds) -> %td total steps\n",
           num_counters, num_threads, duration, total);
  }

  return rv;
}

TEST(counter, multithreaded_test) {
  int inflations = counter_get_total_inflations();
  EXPECT_TRUE(run_test(20, 9, 1000000000, 1) == 0);
  EXPECT_TRUE(inflations != counter_get_total_inflations());
}

#define TEST_ASSERT(p,s) \
  if (!(p)) {                                                           \
    fprintf(stderr, "failure at %s:%d: %s", __FILE__, __LINE__, s);     \
    return -1;                                                          \
  }

static int singlethreaded_test(void (*adder)(counter_t *,int64_t),
                               counter_t c, char const *msg) {
  const int64_t MAX_COUNTER = (((int64_t)1) << 62) - 1;
  const int64_t MIN_COUNTER = -((int64_t)1) << 62;

  /* inflation should stay the same */
  const int lo = c.data & 1;

  adder(&c, -1);
  TEST_ASSERT(counter_get(&c) == -1, msg);
  adder(&c, 2);
  TEST_ASSERT(counter_get(&c) == 1, msg);
  adder(&c, -1);
  TEST_ASSERT(counter_get(&c) == 0, msg);
  adder(&c, MAX_COUNTER);
  TEST_ASSERT(counter_get(&c) == MAX_COUNTER, msg);
  adder(&c, -1);
  TEST_ASSERT(counter_get(&c) == MAX_COUNTER - 1, msg);
  adder(&c, 2);
  TEST_ASSERT(counter_get(&c) == MIN_COUNTER, msg);
  adder(&c, 1);
  TEST_ASSERT(counter_get(&c) == MIN_COUNTER + 1, msg);
  adder(&c, -2);
  TEST_ASSERT(counter_get(&c) == MAX_COUNTER, msg);

  TEST_ASSERT((c.data & 1) == lo, msg);

  return 0;
}

TEST(counter, add_uninflated) {
  counter_t c = { 0 };
  EXPECT_TRUE(singlethreaded_test(counter_add, c,
                                  "counter_add_uninflated") == 0);
}

TEST(counter, add_inflated) {
  counter_t c = { 0 };
  counter_try_inflate(&c);
  EXPECT_TRUE((c.data & 1) == 1);
  EXPECT_TRUE(singlethreaded_test(counter_add, c,
                                  "counter_add_inflated") == 0);
}

TEST(counter, add_nonlocked_uninflated) {
  counter_t c = { 0 };
  EXPECT_TRUE(singlethreaded_test(counter_add_nonlocked, c,
                                  "counter_add_nonlocked_uninflated") == 0);
}

TEST(counter, add_nonlocked_inflated) {
  counter_t c = { 0 };
  counter_try_inflate(&c);
  EXPECT_TRUE((c.data & 1) == 1);
  EXPECT_TRUE(singlethreaded_test(counter_add_nonlocked, c,
                                  "counter_add_nonlocked_inflated") == 0);
}
