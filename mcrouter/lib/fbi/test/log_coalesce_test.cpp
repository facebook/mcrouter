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
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "mcrouter/lib/fbi/debug.h"

#define SLEEP (2*1000)
#define ITERATIONS 3

namespace {

uint64_t to_usec(struct timeval *t)
{
  return t->tv_sec * 1000*1000 + t->tv_usec;
}

void to_timeval(uint64_t usec, struct timeval *t)
{
  t->tv_usec = usec % (1000*1000);
  t->tv_sec = usec / (1000*1000);
}

/* need the same line number for all messages */
void do_msg() {
  dbg_info("hello");
}

void *do_messages(void *p)
{
  struct timeval now;
  struct timeval tmo;
  uint64_t usec;
  int i;

  /* bump the backoff to 32 ms */
  for (i = 0; i < 6; i++) {
    do_msg();
    if (i < 5)
      usleep(SLEEP << i);
  }

  /* generate messages well within the backoff */
  gettimeofday(&now, nullptr);
  usec = to_usec(&now);
  usec += 16*1000;
  to_timeval(usec, &tmo);

  do {
    do_msg();
    gettimeofday(&now, nullptr);
  } while (to_usec(&tmo) > to_usec(&now));

  dbg_info("done");

  return nullptr;
}
}

#define NUM_THREADS 3

TEST(log_coalesce, basic) {
  pthread_t threads[NUM_THREADS];
  int pipe_stderr[2];
  char line[2048];
  FILE *log;
  int i, fd, old_stderr;

  EXPECT_TRUE(pipe(pipe_stderr) == 0);
  old_stderr = dup(STDERR_FILENO);

  dup2(pipe_stderr[1], STDERR_FILENO);
  fd = pipe_stderr[0];
  EXPECT_FALSE(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == -1);
  log = fdopen(fd, "r");
  EXPECT_TRUE(log);

  for (i = 0; i < sizeof(threads)/sizeof(pthread_t); i++)
    EXPECT_TRUE(pthread_create(&threads[i], nullptr,
                               do_messages, nullptr) == 0);

  for (i = 0; i < sizeof(threads)/sizeof(pthread_t); i++)
    EXPECT_TRUE(pthread_join(threads[i], nullptr) == 0);

  /* expect 6 warmup messages one "repeat" and done per-thread */
  for (i = 0; i < NUM_THREADS*(6+1+1); i++)
    EXPECT_TRUE(fgets(line, sizeof(line), log));
  EXPECT_TRUE(fgets(line, sizeof(line), log) == nullptr);

  fclose(log);
  close(pipe_stderr[0]);
  dup2(old_stderr, STDERR_FILENO);
}
