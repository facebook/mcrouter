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
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#include <gtest/gtest.h>

/* NOTE that DEBUG_MAX_LEVEL needs to be defined for runtests_opt
 * so higher level dbg calls aren't optimized out */
#define DEBUG_MAX_LEVEL UINT_MAX
#include "mcrouter/lib/fbi/debug.h"

static sem_t dbg_lock;
struct DebugLockGuard {
  DebugLockGuard() {
    sem_init(&dbg_lock, 0, 1);
  }

  ~DebugLockGuard() {
    sem_destroy(&dbg_lock);
  }
};

static DebugLockGuard debugLockGuard;

static FILE* redir;

/*
 * A convenience function for setting up the
 * rerouting of stderr to a buffer
 */
static void setup_stderr_redir(int* saved_stderr) {
  // by default dbg_*() msgs go to stderr,
  // this pipe setup is used to redirect stderr
  int fd[2];
  EXPECT_EQ(0, pipe(fd));
  *saved_stderr = dup(STDERR_FILENO);

  dup2(fd[1], STDERR_FILENO);
  close(fd[1]);
  redir = fdopen(fd[0], "r");
  EXPECT_FALSE(redir == nullptr);
}

/*
 * A convenience function for cleaning up the setup
 * in setup_stderr_redir()
 */
static void cleanup_stderr_redir(int saved_stderr) {
  // clean up pipe and reset stderr
  dup2(saved_stderr, STDERR_FILENO);
  close(saved_stderr);
  fclose(redir);
}

static void* dbgs(void* arg) {
  EXPECT_EQ(0, sem_wait(&dbg_lock));

  uint saved_lvl = fbi_get_debug();
  fbi_set_debug(UINT_MAX);

  long unsigned exp_tid = (long unsigned) pthread_self();
  long unsigned tid = 0;

#define TID_SCAN_STR "%20lu%*[^\n]"
  dbg_critical("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_error("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_warning("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_notify("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_info("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_debug("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_spew("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_low("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_medium("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  dbg_high("Test dbg");
  EXPECT_EQ(1, fscanf(redir, TID_SCAN_STR, &tid));
  EXPECT_EQ(exp_tid, tid);

  fbi_set_debug(saved_lvl);
  EXPECT_EQ(0, sem_post(&dbg_lock));
  return nullptr;
}

TEST(log_tid, SingleThreadTest) {
  int saved_stderr;
  setup_stderr_redir(&saved_stderr);
  dbgs(nullptr);
  cleanup_stderr_redir(saved_stderr);
}

TEST(log_tid, MultiThreadedTest) {
  int saved_stderr;
  setup_stderr_redir(&saved_stderr);
  pthread_t t1, t2;
  EXPECT_EQ(0, pthread_create(&t1, nullptr, dbgs, nullptr));
  EXPECT_EQ(0, pthread_create(&t2, nullptr, dbgs, nullptr));

  EXPECT_EQ(0, pthread_join(t1, nullptr));
  EXPECT_EQ(0, pthread_join(t2, nullptr));
  cleanup_stderr_redir(saved_stderr);
}
