/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <fcntl.h>

#include <thread>

#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/lib/fbi/asox_semaphore.h"

#define NUM_THREADS 10

namespace {
asox_sem_t sems[NUM_THREADS];

struct test_arg_t {
  int mythread;
  folly::EventBase base;
  int myflag;
  int increment;
  int num_rounds;
};

int value = 0;
bool my_on_signal(asox_sem_t sem, uint64_t num_signals, void *arg) {
  test_arg_t* test_arg = (test_arg_t*) arg;
  value += num_signals;
  test_arg->myflag = 1;
  return true;
}

asox_sem_callbacks_t callbacks = {my_on_signal};

void ping_thread(test_arg_t* test_arg) {
  int peer = (test_arg->mythread + 1) % NUM_THREADS;
  int current_round = 0;

  while (current_round < test_arg->num_rounds) {
    // wait for the peer to signal us.
    // If this is thread 0's first round then
    // start the chain by sending a singal without waiting
    if (test_arg->mythread > 0 || current_round > 0) {
      while (test_arg->myflag == 0) {
        EXPECT_TRUE(test_arg->base.loopOnce());
      }
    } else {
      // account for the fact we skipped the first
      // callback.
      value += test_arg->increment;
    }
    // reset the flag before setting the peer
    // to avoid a race
    test_arg->myflag = 0;
    asox_sem_signal(sems[peer], test_arg->increment);
    current_round ++;
  }
}

/** This test spawns NUM_THREADS threads
 *  and has each thread signal the next thread after
 *  incrementing the value until it reaches
 *  10*NUM_THREADS. Each thread is running the event
 *  base loop on its own event base thus they are
 *  making indepedent progress
 */
void sem_test_common(asox_sem_flags_t flags, int increment, int num_rounds) {
  std::thread threads[NUM_THREADS];
  std::vector<std::unique_ptr<test_arg_t>> args;

  value = 0;
  for (int t = 0; t < NUM_THREADS; t ++) {
    auto arg = folly::make_unique<test_arg_t>();
    arg->mythread = t;
    arg->myflag = 0;
    arg->increment = increment;
    arg->num_rounds = num_rounds;
    sems[t] = asox_sem_new(arg->base.getLibeventBase(),
                           &callbacks, 0, flags, arg.get());
    args.emplace_back(std::move(arg));
  }

  for (int t = 0; t < NUM_THREADS; t ++) {
    threads[t] = std::thread(ping_thread, args[t].get());
  }

  for (int t = 0; t < NUM_THREADS; t ++) {
    threads[t].join();
  }

  for (int t = 0; t < NUM_THREADS; t ++) {
    asox_sem_del(sems[t]);
  }

  EXPECT_TRUE(value == (NUM_THREADS)*increment*num_rounds);
}
}

TEST(libasox_semtest, single_1) {
  sem_test_common((asox_sem_flags_t)0, 1, 1);
  sem_test_common((asox_sem_flags_t)0, 1, 10);
}

TEST(libasox_semtest, single_10) {
  sem_test_common((asox_sem_flags_t)0, 10, 1);
  sem_test_common((asox_sem_flags_t)0, 10, 10);
}

TEST(libasox_semtest, multi_1) {
  sem_test_common((asox_sem_flags_t)ASOX_SEM_MULTI, 1, 1);
  sem_test_common((asox_sem_flags_t)ASOX_SEM_MULTI, 1, 10);
}

TEST(libasox_semtest, multi_10) {
  sem_test_common((asox_sem_flags_t)ASOX_SEM_MULTI, 10, 1);
  sem_test_common((asox_sem_flags_t)ASOX_SEM_MULTI, 10, 10);
}

TEST(libasox_semtest, compat_single_1) {
  sem_test_common((asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | 0), 1, 1);
  sem_test_common((asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | 0), 1, 10);
}

TEST(libasox_semtest, compat_single_10) {
  sem_test_common((asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | 0), 10, 1);
  sem_test_common((asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | 0), 10, 10);
}

TEST(libasox_semtest, compat_multi_1) {
  sem_test_common(
    (asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | ASOX_SEM_MULTI), 1, 1);
  sem_test_common(
    (asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | ASOX_SEM_MULTI), 1, 10);
}

TEST(libasox_semtest, compat_multi_10) {
  sem_test_common(
    (asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | ASOX_SEM_MULTI), 10, 1);
  sem_test_common(
    (asox_sem_flags_t)(ASOX_SEM_COMPATIBILITY_MODE | ASOX_SEM_MULTI), 10, 10);
}

namespace {
bool compat_overflow_on_signal(asox_sem_t sem,
                               uint64_t num_signal,
                               void *arg) {
  uint64_t *val = (uint64_t*)arg;
  (*val)++;
  return true;
}
}

TEST(libasox_semtest, compat_overflow) {
  folly::EventBase base;
  uint64_t total_signals_received = 0;
  asox_sem_callbacks_t cb = {compat_overflow_on_signal};
  asox_sem_t sem = asox_sem_new(base.getLibeventBase(),
                                &cb,
                                0,
                                ASOX_SEM_COMPATIBILITY_MODE,
                                &total_signals_received);
  int fd[2];
  PCHECK(pipe(fd) == 0);
  fcntl(fd[0], F_SETFL, O_NONBLOCK);
  fcntl(fd[1], F_SETFL, O_NONBLOCK);

  uint64_t total_signals_sent = 0;
  while (write(fd[1], "0", 1) > 0) {
    total_signals_sent++;
    asox_sem_signal(sem, 1);
  }
  // both local pipe (fd) and the pipe in asox semaphore
  // should be full now
  // Let's put extra signal
  for (int i = 0; i < 10; i++) {
    total_signals_sent++;
    asox_sem_signal(sem, 1);
  }

  EXPECT_TRUE(base.loopOnce());

  EXPECT_EQ(total_signals_sent, total_signals_received);
  asox_sem_del(sem);
  close(fd[0]);
  close(fd[1]);
}

// This test makes sure that the callback is not called again when it returns
// false (i.e., that it properly early-teminates) when the semaphore is in
// single mode.
TEST(libasox_semtest, early_termination_single_mode) {
  folly::EventBase base;
  uint64_t cnt = 0;
  asox_sem_callbacks_t cb = {
    .on_signal = [](asox_sem_t sem, uint64_t num_signals, void *arg) {
      uint64_t *cnt = (uint64_t *)arg;
      *cnt += num_signals;
      return *cnt < 2;
    },
  };
  asox_sem_t sem = asox_sem_new(base.getLibeventBase(), &cb, 0,
                                ASOX_SEM_NONE, &cnt);

  asox_sem_signal(sem, 10);

  EXPECT_TRUE(base.loopOnce());

  EXPECT_EQ(cnt, 2);
  asox_sem_del(sem);
}

// This test makes sure that the callback is not called again when it returns
// false (i.e., that it properly early-teminates) when the semaphore is in
// multi mode.
TEST(libasox_semtest, early_termination_multi_mode) {
  folly::EventBase base;
  uint64_t cnt = 0;
  asox_sem_callbacks_t cb = {
    .on_signal = [](asox_sem_t sem, uint64_t num_signals, void *arg) {
      uint64_t *cnt = (uint64_t *)arg;
      *cnt += num_signals;
      if (*cnt < 10) {
        asox_sem_signal(sem, 2);
      }
      return *cnt < 4;
    },
  };
  asox_sem_t sem = asox_sem_new(base.getLibeventBase(), &cb, 0,
                                ASOX_SEM_MULTI, &cnt);

  asox_sem_signal(sem, 2);

  EXPECT_TRUE(base.loopOnce());

  EXPECT_EQ(cnt, 4);
  asox_sem_del(sem);
}
