/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <folly/experimental/TestUtil.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>

#include "mcrouter/awriter.h"
#include "mcrouter/proxy.h"

using namespace facebook::memcache::mcrouter;

using folly::test::TemporaryFile;

#define WRITE_STRING "abc\n"
#define WRITE_STRING_LEN (sizeof(WRITE_STRING) - 1)

class AtomicCounter {
public:
  AtomicCounter() {
    cnt = 0;
    pthread_mutex_init(&lock, nullptr);
    pthread_cond_init(&cond, nullptr);
  }

  ~AtomicCounter() {
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
  }

  void notify(int v) {
    pthread_mutex_lock(&lock);
    cnt += v;
    pthread_mutex_unlock(&lock);

    pthread_cond_broadcast(&cond);
  }

  void reset() {
    pthread_mutex_lock(&lock);
    cnt = 0;
    pthread_mutex_unlock(&lock);
  }

  void wait(std::function<bool(int)> f) {
    pthread_mutex_lock(&lock);
    while (!f(cnt)) {
      pthread_cond_wait(&cond, &lock);
    }
    pthread_mutex_unlock(&lock);
  }

private:
  pthread_cond_t cond;
  pthread_mutex_t lock;
  int cnt;
};

struct counts {
  int success;
  int failure;

  AtomicCounter cnt;

  counts() { success = failure = 0; }

  void reset() {
    success = 0;
    failure = 0;
    cnt.reset();
  }
};

struct testing_context_t {
  counts *counter;
  writelog_entry_t log_context;
};

void callback_counter(awriter_entry_t *e, int result) {
  testing_context_t *w = (testing_context_t*)e->context;
  if (result) {
    w->counter->failure++;
  } else {
    w->counter->success++;
  }

  w->counter->cnt.notify(1);
}

static int test_entry_writer(awriter_entry_t* e) {
  int ret;
  writelog_entry_t *entry = &((testing_context_t*)e->context)->log_context;
  ssize_t size =
    folly::writeFull(entry->fd->fd, entry->buf, entry->size);
  if (size == -1) {
    ret = errno;
  } else if (size < entry->size) {
    ret = EIO;
  } else {
    ret = 0;
  }

  return ret;
}

static const awriter_callbacks_t test_callbacks = {
  &callback_counter,
  &test_entry_writer
};

// Simple test that creates a number of async writers and
// then destroys them.
TEST(awriter, create_destroy) {
  const int num_entries = 20;
  std::unique_ptr<awriter_t> w[num_entries];
  size_t i;

  for (i = 0; i < num_entries; i++) {
    w[i] = folly::make_unique<awriter_t>(i);
    EXPECT_TRUE(w[i] != nullptr);
  }
}

// Test that creates an async writer, writes a few things
// to it and checks that the writes complete with succes
// and that the file is written to.
TEST(awriter, sanity) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  int ret;
  pthread_t thread;
  std::unique_ptr<awriter_t> w;
  const int num_entries = 3;
  testing_context_t e[num_entries];
  struct stat s;
  countedfd_t cfd;
  cfd.fd = f.fd();
  cfd.refcount = 1;

  w = folly::make_unique<awriter_t>(0);
  EXPECT_TRUE(w != nullptr);

  ret = pthread_create(&thread, nullptr, awriter_thread_run, w.get());
  EXPECT_EQ(ret, 0);

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.fd = &cfd;
    e[i].log_context.buf = WRITE_STRING;
    e[i].log_context.size = WRITE_STRING_LEN;
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    ret = awriter_queue(w.get(), &e[i].log_context.awentry);
    EXPECT_EQ(ret, 0);
  }

  testCounter.cnt.wait([](int v) { return v >= num_entries; });

  awriter_stop(w.get());
  pthread_join(thread, nullptr);
  EXPECT_EQ(testCounter.success, num_entries);

  ret = fstat(f.fd(), &s);
  EXPECT_EQ(ret, 0);

  EXPECT_EQ(s.st_size, num_entries * WRITE_STRING_LEN);
}

// Test that ensures that pending items in the queue are
// flushed when the writer is stopped.
TEST(awriter, flush_queue) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  int ret;
  pthread_t thread;
  std::unique_ptr<awriter_t> w;
  const int num_entries = 10;
  testing_context_t e[num_entries];

  w = folly::make_unique<awriter_t>(0);
  EXPECT_TRUE(w != nullptr);

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.buf = WRITE_STRING;
    e[i].log_context.size = WRITE_STRING_LEN;
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    ret = awriter_queue(w.get(), &e[i].log_context.awentry);
    EXPECT_EQ(ret, 0);
  }

  // Stop the queue even before we start the thread.
  awriter_stop(w.get());

  ret = pthread_create(&thread, nullptr, awriter_thread_run, w.get());
  EXPECT_EQ(ret, 0);

  pthread_join(thread, nullptr);

  EXPECT_EQ(testCounter.failure, num_entries);
}

// Test that ensures that maximum queue length is honored.
TEST(awriter, max_queue_length) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  int ret;
  pthread_t thread;
  std::unique_ptr<awriter_t> w;
  const int maxlen = 5;
  const int num_entries = maxlen + 5;
  testing_context_t e[num_entries];
  countedfd_t cfd;
  cfd.fd = f.fd();
  cfd.refcount = 1;

  w = folly::make_unique<awriter_t>(maxlen);
  EXPECT_TRUE(w != nullptr);

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.fd = &cfd;
    e[i].log_context.buf = WRITE_STRING;
    e[i].log_context.size = WRITE_STRING_LEN;
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    ret = awriter_queue(w.get(), &e[i].log_context.awentry);
    if (i < maxlen) {
      EXPECT_EQ(ret, 0);
    } else {
      EXPECT_EQ(ret, ENOSPC);
    }
  }

  // Create the thread to process the requests and wait for all
  // of them to be completed.
  ret = pthread_create(&thread, nullptr, awriter_thread_run, w.get());
  EXPECT_EQ(ret, 0);

  testCounter.cnt.wait([](int v) { return v >= maxlen; });

  EXPECT_EQ(testCounter.success, maxlen);

  // Make sure we can submit an entry again.
  ret = awriter_queue(w.get(), &e[0].log_context.awentry);
  EXPECT_EQ(ret, 0);

  // Stop everything.
  awriter_stop(w.get());
  pthread_join(thread, nullptr);
}

// Test that passes invalid fd and expect errors when writing.
TEST(awriter, invalid_fd) {
  counts testCounter;
  int ret;
  pthread_t thread;
  std::unique_ptr<awriter_t> w;
  const int num_entries = 3;
  testing_context_t e[num_entries];
  countedfd_t cfd;
  cfd.fd = -1;
  cfd.refcount = 1;

  w = folly::make_unique<awriter_t>(0);
  EXPECT_TRUE(w != nullptr);

  ret = pthread_create(&thread, nullptr, awriter_thread_run, w.get());
  EXPECT_EQ(ret, 0);

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.fd = &cfd;
    e[i].log_context.buf = WRITE_STRING;
    e[i].log_context.size = WRITE_STRING_LEN;
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    ret = awriter_queue(w.get(), &e[i].log_context.awentry);
    EXPECT_EQ(ret, 0);
  }

  testCounter.cnt.wait([](int v) { return v >= num_entries; });

  awriter_stop(w.get());
  pthread_join(thread, nullptr);

  EXPECT_EQ(testCounter.failure, num_entries);
}
