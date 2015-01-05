/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/lib/fbi/asox_queue.h"

#define NUM_THREADS 10

#define FLOOD_MODE 1
#define PING_PONG_MODE 2

struct thread_arg_t {
  int mythread;
  folly::EventBase base;
  int myflag;
  int increment;
  int num_rounds;
  int mode;
};

namespace {
asox_queue_t queues[NUM_THREADS];
std::atomic<int> queue_value;

void my_elem_ready(asox_queue_t q, asox_queue_entry_t *entry, void *arg) {
  thread_arg_t* thread_arg = (thread_arg_t*) arg;
  int inval;
  EXPECT_EQ(sizeof(int), entry->nbytes);
  inval = (*((int*) entry->data));
  thread_arg->myflag += 1;
  queue_value += inval;
  free(entry->data);
  return;
}

asox_queue_callbacks_t queue_callbacks = {my_elem_ready, nullptr};

void queue_thread(thread_arg_t *thread_arg) {
  int peer = (thread_arg->mythread + 1) % NUM_THREADS;


  if (thread_arg->mode == FLOOD_MODE) {
    // enqueue everything for my peer
    for (int i = 0; i < thread_arg->num_rounds; i ++) {
      asox_queue_entry_t temp;
      temp.data = (char*) malloc(sizeof(int));
      *((int*) temp.data) = thread_arg->increment;
      temp.nbytes = sizeof(int);
      temp.priority = 0;
      asox_queue_enqueue(queues[peer], &temp);
    }

    //wait for everything to be done
    while (thread_arg->myflag != thread_arg->num_rounds) {
      EXPECT_TRUE(thread_arg->base.loopOnce());
    }
  } else {
    for (int i = 0; i < thread_arg->num_rounds; i ++) {
      // enqueue one element and wait for the peer to
      // signal me that i'm ready to do the next round
      asox_queue_entry_t temp;
      temp.data = (char*) malloc(sizeof(int));
      *((int*) temp.data) = thread_arg->increment;
      temp.nbytes = sizeof(int);
      temp.priority = 0;
      asox_queue_enqueue(queues[peer], &temp);

      while (thread_arg->myflag < i+1) {
        EXPECT_TRUE(thread_arg->base.loopOnce());
      }
    }
  }
}

/**
 *  This test spawns NUM_THREADS threads
 *  and has each thread signal the next thread after
 *  incrementing the queue_value until it reaches
 *  num_rounds*NUM_THREADS. Each thread is running the event
 *  base loop on its own event base thus they are
 *  making indepedent progress
 */
void queue_test_common(int num_rounds, int increment, int mode) {
  std::thread threads[NUM_THREADS];
  std::vector<std::unique_ptr<thread_arg_t>> args;

  queue_value = 0;

  for (int t = 0; t < NUM_THREADS; t ++) {
    auto arg = folly::make_unique<thread_arg_t>();
    arg->mythread = t;
    arg->myflag = 0;
    arg->increment = increment;
    arg->num_rounds = num_rounds;
    arg->mode = mode;
    queues[t] = asox_queue_init(arg->base.getLibeventBase(), 0, 1, 0, 0,
                                &queue_callbacks,
                                ASOX_QUEUE_INTRA_PROCESS, arg.get());
    args.emplace_back(std::move(arg));
  }

  for (int t = 0; t < NUM_THREADS; t ++) {
    threads[t] = std::thread(queue_thread, args[t].get());
  }

  for (int t = 0; t < NUM_THREADS; t ++) {
    threads[t].join();
  }

  for (int t = 0; t < NUM_THREADS; t ++) {
    asox_queue_del(queues[t]);
  }

  EXPECT_EQ((NUM_THREADS)*increment*num_rounds, queue_value);
}
}

TEST(libasox_queuetest, flood) {
  queue_test_common(1, 1, FLOOD_MODE);
  queue_test_common(1, 10, FLOOD_MODE);
  queue_test_common(10, 1, FLOOD_MODE);
  queue_test_common(10, 10, FLOOD_MODE);
}


TEST(libasox_queuetest, pingpong) {
  queue_test_common(1, 1, PING_PONG_MODE);
  queue_test_common(1, 10, PING_PONG_MODE);
  queue_test_common(10, 1, PING_PONG_MODE);
  queue_test_common(10, 10, PING_PONG_MODE);
}

namespace {
void multi_count_client(asox_queue_t queue, int N, int nentries, int *running) {
  asox_queue_entry_t entries[nentries];
  for (int i = 0; i < nentries; i++) {
    entries[i].data = nullptr;
    entries[i].nbytes = 0;
    entries[i].priority = 0;
    entries[i].type = i + 1;
  }
  for (int i = 0; i < N; i++) {
    asox_queue_multi_enqueue(queue, entries, nentries);
  }
  __sync_fetch_and_add(running, -1);

}

void count_elem_ready(asox_queue_t q, asox_queue_entry_t *entry, void *arg) {
  int *count = (int*)arg;
  *count += entry->type;
}

void libasox_queuetest_multicount_common(asox_queue_flags_t flags) {
  int counter = 0;
  int running = NUM_THREADS;
  std::thread threads[NUM_THREADS];
  folly::EventBase base;
  asox_queue_callbacks_t callbacks = {count_elem_ready};
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, 0, 0,
                                       &callbacks, flags, &counter);
  const int nentries = 20;
  const int N = 10000;

  for (int t = 0; t < NUM_THREADS; t++) {
    threads[t] = std::thread(multi_count_client, queue, N, nentries, &running);
  }

  while (running) {
    EXPECT_TRUE(base.loopOnce());
  }

  for (int t = 0; t < NUM_THREADS; t++) {
    threads[t].join();
  }

  EXPECT_EQ(N * NUM_THREADS * ((nentries * (nentries + 1)) / 2), counter);
  asox_queue_del(queue);
}
}

TEST(libasox_queuetest, multicount) {
  libasox_queuetest_multicount_common(ASOX_QUEUE_INTRA_PROCESS);
}

namespace {

struct counts {
  std::atomic<size_t> ready{0};
  std::atomic<size_t> sweep{0};
};

asox_queue_callbacks_t counter_test_callbacks = {
  .on_elem_ready = [](asox_queue_t q, asox_queue_entry_t *entry, void *arg) {
    counts *c = (counts*)arg;
    c->ready++;
  },
  .on_elem_sweep = [](asox_queue_t q, asox_queue_entry_t *entry, void *arg) {
    counts *c = (counts*)arg;
    c->sweep++;
  },
};
}

TEST(libasox_queuetest, sweep) {
  folly::EventBase base;
  counts cnt;
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, 0, 0,
                                       &counter_test_callbacks,
                                       ASOX_QUEUE_INTRA_PROCESS,
                                       &cnt);

  const int num_entry = 7;
  asox_queue_entry_t entry;
  entry.priority = 0;
  entry.nbytes = 0;
  for (int i = 0; i < num_entry; i++) {
    asox_queue_enqueue(queue, &entry);
  }

  asox_queue_del(queue);
  EXPECT_EQ(cnt.sweep, num_entry);
  EXPECT_EQ(cnt.ready, 0);
}

TEST(libasox_queuetest, atonce) {
  counts cnt;
  const int at_once = 2;
  folly::EventBase base;
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, 0, at_once,
                                       &counter_test_callbacks,
                                       ASOX_QUEUE_INTRA_PROCESS,
                                       &cnt);
  const int num_entry = at_once * 4;
  int i;

  asox_queue_entry_t entry;
  entry.priority = 0;
  entry.nbytes = 0;
  for (i = 0; i < num_entry; i++) {
    asox_queue_enqueue(queue, &entry);
  }

  for (i = 0; i < num_entry / at_once; i++) {
    EXPECT_TRUE(base.loopOnce());
    EXPECT_EQ(cnt.ready, (i + 1) * at_once);
  }

  EXPECT_EQ(cnt.sweep, 0);

  asox_queue_del(queue);
}

TEST(libasox_queuetest, disconnect) {
  counts cnt;
  folly::EventBase base;
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, 0, 0,
                                       &counter_test_callbacks,
                                       ASOX_QUEUE_INTRA_PROCESS,
                                       &cnt);
  const int num_entry = 8;
  int i;

  asox_queue_entry_t entry;
  entry.priority = 0;
  entry.nbytes = 0;
  for (i = 0; i < num_entry; i++) {
    asox_queue_enqueue(queue, &entry);
  }

  asox_queue_disconnect(queue);

  EXPECT_TRUE(base.loopOnce());

  EXPECT_EQ(cnt.sweep, num_entry);
  EXPECT_EQ(cnt.ready, 0);

  asox_queue_del(queue);
}

// This tests the remote disconnect functionality. It queues 4 entries that
// take 3 seconds to finish each, waits 1 second and then disconnects. The
// expectation is that we attempt to disconnect while the first entry is
// running; it should be allowed to complete, but the remaining queued ones
// should have been swept, by the time disconnect returns. The loop should
// also return asox_disconnected, indicating that there are no more events.
TEST(libasox_queuetest, remote_disconnect) {
  asox_queue_callbacks_t cb = {
    .on_elem_ready = [](asox_queue_t q, asox_queue_entry_t *entry, void *arg) {
      counts *c = (counts*)arg;
      sleep(3);
      c->ready++;
    },
    .on_elem_sweep = [](asox_queue_t q, asox_queue_entry_t *entry, void *arg) {
      counts *c = (counts*)arg;
      c->sweep++;
    },
  };
  counts cnt;
  folly::EventBase base;
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, 0, 0, &cb,
                                       ASOX_QUEUE_INTRA_PROCESS,
                                       &cnt);
  const int num_entry = 4;
  int i;
  asox_queue_entry_t entry;

  std::thread loop{
    [&] () {
      EXPECT_TRUE(base.loop());
    }
  };

  entry.priority = 0;
  entry.nbytes = 0;
  for (i = 0; i < num_entry; i++) {
    asox_queue_enqueue(queue, &entry);
  }

  sleep(1);

  asox_queue_remote_disconnect(queue);

  EXPECT_EQ(cnt.ready, 1);
  EXPECT_EQ(cnt.sweep, num_entry - 1);

  loop.join();

  asox_queue_del(queue);
}

namespace {
struct Info {
  std::thread thread;
  int count;
  asox_queue_entry_t *entries;
  pthread_rwlock_t *lock;
  asox_queue_t q;

  Info() {}
  Info(const Info&) = delete;
  Info& operator=(const Info&) = delete;

  ~Info() {
    free(entries);
  }
};

std::vector<std::unique_ptr<Info>> concurrent_queueing(asox_queue_t queue,
                                                       size_t thread_count,
                                                       int entries_per_thread) {
  pthread_rwlock_t lock;
  timespec ts;
  double before;
  double v;

  pthread_rwlock_init(&lock, nullptr);
  pthread_rwlock_wrlock(&lock);

  // Initialize all threads.
  std::vector<std::unique_ptr<Info>> ti;
  for (size_t i = 0; i < thread_count; i++) {
    auto info = folly::make_unique<Info>();
    info->q = queue;
    info->lock = &lock;
    info->count = entries_per_thread;
    info->entries = (asox_queue_entry_t*)calloc(entries_per_thread,
                                                sizeof(asox_queue_entry_t));
    info->thread = std::thread([](Info *t) {
        pthread_rwlock_rdlock(t->lock);
        for (int j = 0; j < t->count; j++) {
          asox_queue_enqueue_nocopy(t->q, t->entries + j);
        }
        pthread_rwlock_unlock(t->lock);
      }, info.get());
    ti.emplace_back(std::move(info));
  }

  // Let threads go and wait for them to finish.
  clock_gettime(CLOCK_MONOTONIC, &ts);
  before = ts.tv_sec * 1e9 + ts.tv_nsec;

  pthread_rwlock_unlock(&lock);
  for (auto& i : ti) {
    i->thread.join();
  }
  clock_gettime(CLOCK_MONOTONIC, &ts);
  v = ts.tv_sec * 1e9 + ts.tv_nsec - before;
  v /= thread_count * entries_per_thread;
  printf("Time per entry: %lfns\n", v);

  return ti;
}
}

TEST(libasox_queuetest, concurrent_enqueuing) {
  counts cnt;
  folly::EventBase base;
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, 0, 0,
                                       &counter_test_callbacks,
                                       ASOX_QUEUE_INTRA_PROCESS,
                                       &cnt);
  const int thread_count = 5;
  const int entry_count = 100000;

  std::thread loop{
    [&] () {
      EXPECT_TRUE(base.loop());
    }
  };

  auto ti = concurrent_queueing(queue, thread_count, entry_count);

  while (cnt.ready != thread_count * entry_count) {
    usleep(100);
  }

  asox_queue_remote_disconnect(queue);
  loop.join();

  EXPECT_EQ(cnt.ready, thread_count * entry_count);
  EXPECT_EQ(cnt.sweep, 0);

  asox_queue_del(queue);
}

TEST(libasox_queuetest, queuelimit) {
  counts cnt;
  folly::EventBase base;
  const int limit = 5;
  asox_queue_t queue = asox_queue_init(base.getLibeventBase(), 0, 1, limit, 0,
                                       &counter_test_callbacks,
                                       ASOX_QUEUE_INTRA_PROCESS,
                                       &cnt);
  int i;
  int ret;

  asox_queue_entry_t entry;
  entry.priority = 0;
  entry.nbytes = 0;
  for (i = 0; i < limit; i++) {
    ret = asox_queue_enqueue(queue, &entry);
    EXPECT_EQ(ret, 1);
  }

  for (i = 0; i < limit; i++) {
    ret = asox_queue_enqueue(queue, &entry);
    EXPECT_EQ(ret, 0);
  }

  asox_queue_disconnect(queue);

  EXPECT_EQ(cnt.sweep, limit);
  EXPECT_EQ(cnt.ready, 0);

  asox_queue_del(queue);
}

TEST(libasox_queuetest, disconnect_from_callback) {
  struct Methods {
    static void ready(asox_queue_t q,
                      asox_queue_entry_t* e,
                      void* arg) {
      asox_queue_disconnect(q);
      delete (int*)(e->data);
      auto running = reinterpret_cast<std::atomic<bool>*>(arg);
      *running = false;
    }
    static void sweep(asox_queue_t q,
                      asox_queue_entry_t* e,
                      void* arg) {
    }
  };

  static asox_queue_callbacks_t disconnect_test_callbacks{
    &Methods::ready,
    &Methods::sweep
  };
  folly::EventBase base;
  std::atomic<bool> running{true};
  auto q = asox_queue_init(base.getLibeventBase(), 0, 1, 5, 0,
                           &disconnect_test_callbacks,
                           ASOX_QUEUE_INTRA_PROCESS,
                           &running);
  std::thread client{
    [&q]() {
      asox_queue_entry_t e;
      e.data = new int();
      e.nbytes = sizeof(int);
      e.priority = 0;
      asox_queue_enqueue(q, &e);
    }};
  while (running) {
    EXPECT_TRUE(base.loopOnce());
  }
  client.join();
  asox_queue_del(q);
}
