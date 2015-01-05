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

#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "mcrouter/lib/fbi/cpp/sfrlock.h"
#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/fbi/sfrlock.h"
#include "mcrouter/lib/fbi/test/test_util.h"

const unsigned repeat = 1000000U;

TEST(sfrlock, concurrent_reads_sanity) {
  const unsigned thread_count = 20;
  sfrlock_t sfrlock;

  sfrlock_init(&sfrlock);

  measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          sfrlock_rdlock(&sfrlock);
          sfrlock_rdunlock(&sfrlock);
        }

        sfrlock_rdlock(&sfrlock);
      });
}

TEST(sfrlock, concurrent_reads_sanity_cpp) {
  const unsigned thread_count = 20;
  SFRLock sfrlock;

  measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          std::lock_guard<SFRReadLock> lg(sfrlock.readLock());
        }

        sfrlock.readLock().lock();
      });
}

TEST(sfrlock, contended_writes_sanity) {
  unsigned cnt = 0;
  const unsigned thread_count = 5;
  sfrlock_t sfrlock;

  sfrlock_init(&sfrlock);

  measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          sfrlock_wrlock(&sfrlock);
          cnt++;
          sfrlock_wrunlock(&sfrlock);
        }
      });
  EXPECT_EQ(cnt, thread_count * repeat);
}

TEST(sfrlock, contended_writes_sanity_cpp) {
  unsigned cnt = 0;
  const unsigned thread_count = 5;
  SFRLock sfrlock;

  measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          {
            std::lock_guard<SFRWriteLock> lg(sfrlock.writeLock());
            cnt++;
          }
        }
      });
  EXPECT_EQ(cnt, thread_count * repeat);
}

TEST(sfrlock, contended_reads_write_sanity) {
  volatile unsigned cnt = 0;
  const unsigned thread_count = 5;
  const unsigned inc = 100;
  sfrlock_t sfrlock;

  sfrlock_init(&sfrlock);

  measure_time_concurrent(thread_count, [&] (unsigned idx) {
        if (!idx) {
          for (unsigned j = repeat / 5; j; j--) {
            sfrlock_wrlock(&sfrlock);
            for (unsigned k = inc; k; k--) {
              cnt++;
            }
            sfrlock_wrunlock(&sfrlock);
            usleep(1);
          }
        } else {
          for (unsigned j = repeat; j; j--) {
            sfrlock_rdlock(&sfrlock);
            EXPECT_EQ(cnt % inc, 0);
            sfrlock_rdunlock(&sfrlock);
          }
        }
      });
  EXPECT_EQ(cnt, inc * repeat / 5);
}

TEST(sfrlock, contended_reads_write_sanity_cpp) {
  volatile unsigned cnt = 0;
  const unsigned thread_count = 5;
  const unsigned inc = 100;
  SFRLock sfrlock;

  measure_time_concurrent(thread_count, [&] (unsigned idx) {
        if (!idx) {
          for (unsigned j = repeat / 5; j; j--) {
            {
              std::lock_guard<SFRWriteLock> lg(sfrlock.writeLock());
              for (unsigned k = inc; k; k--) {
                cnt++;
              }
            }
            usleep(1);
          }
        } else {
          for (unsigned j = repeat; j; j--) {
            {
              std::lock_guard<SFRReadLock> lg(sfrlock.readLock());
              EXPECT_EQ(cnt % inc, 0);
            }
          }
        }
      });
  EXPECT_EQ(cnt, inc * repeat / 5);
}

TEST(sfrlock, contended_reads_writes_sanity) {
  volatile unsigned cnt = 0;
  const unsigned thread_count = 10;
  const unsigned inc = 100;
  sfrlock_t sfrlock;

  sfrlock_init(&sfrlock);

  measure_time_concurrent(thread_count, [&] (unsigned idx) {
        if (idx < thread_count / 2) {
          for (unsigned j = repeat / 5; j; j--) {
            sfrlock_wrlock(&sfrlock);
            for (unsigned k = inc; k; k--) {
              cnt++;
            }
            sfrlock_wrunlock(&sfrlock);
            usleep(1);
          }
        } else {
          for (unsigned j = repeat; j; j--) {
            sfrlock_rdlock(&sfrlock);
            EXPECT_EQ(cnt % inc, 0);
            sfrlock_rdunlock(&sfrlock);
          }
        }
      });
  EXPECT_EQ(cnt, inc * (repeat / 5) * (thread_count / 2));
}

TEST(sfrlock, contended_reads_writes_sanity_cpp) {
  volatile unsigned cnt = 0;
  const unsigned thread_count = 10;
  const unsigned inc = 100;
  SFRLock sfrlock;

  measure_time_concurrent(thread_count, [&] (unsigned idx) {
        if (idx < thread_count / 2) {
          for (unsigned j = repeat / 5; j; j--) {
            {
              std::lock_guard<SFRWriteLock> lg(sfrlock.writeLock());
              for (unsigned k = inc; k; k--) {
                cnt++;
              }
            }
            usleep(1);
          }
        } else {
          for (unsigned j = repeat; j; j--) {
            {
              std::lock_guard<SFRReadLock> lg(sfrlock.readLock());
              EXPECT_EQ(cnt % inc, 0);
            }
          }
        }
      });
  EXPECT_EQ(cnt, inc * (repeat / 5) * (thread_count / 2));
}

TEST(sfrlock, uncontended_read_cost) {
  double t;
  double r;
  pthread_mutex_t mutex;
  pthread_rwlock_t rwlock;
  sfrlock_t sfrlock;

  sfrlock_init(&sfrlock);
  pthread_rwlock_init(&rwlock, nullptr);
  pthread_mutex_init(&mutex, nullptr);

  r = measure_time([&] () {
        for (unsigned cnt = repeat; cnt; cnt--) {
          sfrlock_rdlock(&sfrlock);
          sfrlock_rdunlock(&sfrlock);
        }
      });
  printf("sfrlock_t time: %lf ms\n", r / 1e6);

  t = measure_time([&] () {
        for (unsigned cnt = repeat; cnt; cnt--) {
          pthread_rwlock_rdlock(&rwlock);
          pthread_rwlock_unlock(&rwlock);
        }
      });
  printf("pthread_rwlock_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  t = measure_time([&] () {
        for (unsigned cnt = repeat; cnt; cnt--) {
          pthread_mutex_lock(&mutex);
          pthread_mutex_unlock(&mutex);
        }
      });
  printf("pthread_mutex_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  pthread_rwlock_destroy(&rwlock);
  pthread_mutex_destroy(&mutex);
}

TEST(sfrlock, uncontended_write_cost) {
  double t;
  double r;
  pthread_mutex_t mutex;
  pthread_rwlock_t rwlock;
  sfrlock_t sfrlock;

  sfrlock_init(&sfrlock);
  pthread_rwlock_init(&rwlock, nullptr);
  pthread_mutex_init(&mutex, nullptr);

  r = measure_time([&] () {
        for (unsigned cnt = repeat; cnt; cnt--) {
          sfrlock_wrlock(&sfrlock);
          sfrlock_wrunlock(&sfrlock);
        }
      });
  printf("sfrlock_t time: %lf ms\n", r / 1e6);

  t = measure_time([&] () {
        for (unsigned cnt = repeat; cnt; cnt--) {
          pthread_rwlock_wrlock(&rwlock);
          pthread_rwlock_unlock(&rwlock);
        }
      });
  printf("pthread_rwlock_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  t = measure_time([&] () {
        for (unsigned cnt = repeat; cnt; cnt--) {
          pthread_mutex_lock(&mutex);
          pthread_mutex_unlock(&mutex);
        }
      });
  printf("pthread_mutex_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  pthread_rwlock_destroy(&rwlock);
  pthread_mutex_destroy(&mutex);
}

TEST(sfrlock, concurrent_read_cost) {
  double t;
  double r;
  const unsigned thread_count = 5;
  sfrlock_t sfrlock;
  pthread_rwlock_t rwlock;
  pthread_mutex_t mutex;

  sfrlock_init(&sfrlock);
  pthread_rwlock_init(&rwlock, nullptr);
  pthread_mutex_init(&mutex, nullptr);

  r = measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          sfrlock_rdlock(&sfrlock);
          sfrlock_rdunlock(&sfrlock);
        }
      });
  printf("sfrlock_t time: %lf ms\n", r / 1e6);

  t = measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          pthread_rwlock_rdlock(&rwlock);
          pthread_rwlock_unlock(&rwlock);
        }
      });
  printf("pthread_rwlock_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  t = measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          pthread_mutex_lock(&mutex);
          pthread_mutex_unlock(&mutex);
        }
      });
  printf("pthread_mutex_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  pthread_rwlock_destroy(&rwlock);
  pthread_mutex_destroy(&mutex);
}

TEST(sfrlock, contended_write_cost) {
  double t;
  double r;
  const unsigned thread_count = 5;
  sfrlock_t sfrlock;
  pthread_rwlock_t rwlock;
  pthread_mutex_t mutex;

  sfrlock_init(&sfrlock);
  pthread_rwlock_init(&rwlock, nullptr);
  pthread_mutex_init(&mutex, nullptr);

  r = measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          sfrlock_wrlock(&sfrlock);
          sfrlock_wrunlock(&sfrlock);
        }
      });
  printf("sfrlock_t time: %lf ms\n", r / 1e6);

  t = measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          pthread_rwlock_wrlock(&rwlock);
          pthread_rwlock_unlock(&rwlock);
        }
      });
  printf("pthread_rwlock_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  t = measure_time_concurrent(thread_count, [&] (unsigned) {
        for (unsigned j = repeat; j; j--) {
          pthread_mutex_lock(&mutex);
          pthread_mutex_unlock(&mutex);
        }
      });
  printf("pthread_mutex_t time: %lf ms (%+.2lf%%)\n", t / 1e6,
         -(1 - (t / r)) * 100);

  pthread_rwlock_destroy(&rwlock);
  pthread_mutex_destroy(&mutex);
}

void sfrlock_read_test(void) {
  sfrlock_t lock;
  sfrlock_init(&lock);

  sfrlock_rdlock(&lock);
  sfrlock_rdunlock(&lock);
}

void sfrlock_write_test(void) {
  sfrlock_t lock;
  sfrlock_init(&lock);

  sfrlock_wrlock(&lock);
  sfrlock_wrunlock(&lock);
}

/**
 * This is really only used to easily inspect the asm code generation on
 * gdb by diassembling the functions above.
 */
TEST(sfrlock, trivial_acquisition) {
  sfrlock_read_test();
  sfrlock_write_test();
}
