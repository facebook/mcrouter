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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <folly/experimental/TestUtil.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/File.h>

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

struct writelog_entry_t {
  std::shared_ptr<folly::File> file;
  std::string buf;
  awriter_entry_t awentry;
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
  writelog_entry_t *entry = &((testing_context_t*)e->context)->log_context;
  ssize_t size =
    folly::writeFull(entry->file->fd(),
                     entry->buf.data(),
                     entry->buf.size());
  if (size == -1) {
    return errno;
  }
  if (static_cast<size_t>(size) < entry->buf.size()) {
    return EIO;
  }
  return 0;
}

static const awriter_callbacks_t test_callbacks = {
  &callback_counter,
  &test_entry_writer
};

// Simple test that creates a number of async writers and
// then destroys them.
TEST(awriter, create_destroy) {
  const int num_entries = 20;
  std::unique_ptr<AsyncWriter> w[num_entries];
  size_t i;

  for (i = 0; i < num_entries; i++) {
    w[i] = folly::make_unique<AsyncWriter>(i);
  }
}

// Test that creates an async writer, writes a few things
// to it and checks that the writes complete with succes
// and that the file is written to.
TEST(awriter, sanity) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  const int num_entries = 3;
  testing_context_t e[num_entries];
  struct stat s;
  auto fd = std::make_shared<folly::File>(f.fd());

  auto w = folly::make_unique<AsyncWriter>();
  EXPECT_TRUE(w->start("awriter:test"));

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.file = fd;
    e[i].log_context.buf = std::string(WRITE_STRING, WRITE_STRING_LEN);
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    EXPECT_TRUE(awriter_queue(w.get(), &e[i].log_context.awentry));
  }

  testCounter.cnt.wait([](int v) { return v >= num_entries; });

  w.reset();
  EXPECT_EQ(testCounter.success, num_entries);

  EXPECT_EQ(fstat(f.fd(), &s), 0);

  EXPECT_EQ(s.st_size, num_entries * WRITE_STRING_LEN);
}

// Test that ensures that pending items in the queue are
// flushed when the writer is stopped.
TEST(awriter, flush_queue) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  const int num_entries = 10;
  testing_context_t e[num_entries];

  auto w = folly::make_unique<AsyncWriter>(0);

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.buf = std::string(WRITE_STRING, WRITE_STRING_LEN);
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    EXPECT_TRUE(awriter_queue(w.get(), &e[i].log_context.awentry));
  }

  // Stop the writer even before we start the thread.
  w->stop();

  EXPECT_EQ(testCounter.failure, num_entries);
}

// Test that ensures that maximum queue length is honored.
TEST(awriter, max_queue_length) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  const int maxlen = 5;
  const int num_entries = maxlen + 5;
  testing_context_t e[num_entries];
  auto fd = std::make_shared<folly::File>(f.fd());

  auto w = folly::make_unique<AsyncWriter>(maxlen);
  EXPECT_TRUE(w != nullptr);

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.file = fd;
    e[i].log_context.buf = std::string(WRITE_STRING, WRITE_STRING_LEN);
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    bool ret = awriter_queue(w.get(), &e[i].log_context.awentry);
    if (i < maxlen) {
      EXPECT_TRUE(ret);
    } else {
      EXPECT_FALSE(ret);
    }
  }

  // Create the thread to process the requests and wait for all
  // of them to be completed.
  EXPECT_TRUE(w->start("awriter:test"));

  testCounter.cnt.wait([](int v) { return v >= maxlen; });

  EXPECT_EQ(testCounter.success, maxlen);

  // Make sure we can submit an entry again.
  EXPECT_TRUE(awriter_queue(w.get(), &e[0].log_context.awentry));
}

// Test that passes invalid fd and expect errors when writing.
TEST(awriter, invalid_fd) {
  counts testCounter;
  const int num_entries = 3;
  testing_context_t e[num_entries];
  auto fd = std::make_shared<folly::File>(-1);

  auto w = folly::make_unique<AsyncWriter>(0);
  EXPECT_TRUE(w->start("awriter:test"));

  for (int i = 0; i < num_entries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.file = fd;
    e[i].log_context.buf = std::string(WRITE_STRING, WRITE_STRING_LEN);
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    EXPECT_TRUE(awriter_queue(w.get(), &e[i].log_context.awentry));
  }

  testCounter.cnt.wait([](int v) { return v >= num_entries; });

  w->stop();

  EXPECT_EQ(testCounter.failure, num_entries);
}
