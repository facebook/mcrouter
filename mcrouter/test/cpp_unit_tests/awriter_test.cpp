/*
 *  Copyright (c) 2016, Facebook, Inc.
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
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/Range.h>

#include "mcrouter/awriter.h"
#include "mcrouter/proxy.h"

using namespace facebook::memcache::mcrouter;

using folly::test::TemporaryFile;

namespace {

constexpr folly::StringPiece kWriteString = "abc\n";

class AtomicCounter {
public:
  AtomicCounter() {
    pthread_mutex_init(&lock, nullptr);
    pthread_cond_init(&cond, nullptr);
  }

  ~AtomicCounter() {
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
  }

  void notify(size_t v) {
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

  void wait(std::function<bool(size_t)> f) {
    pthread_mutex_lock(&lock);
    while (!f(cnt)) {
      pthread_cond_wait(&cond, &lock);
    }
    pthread_mutex_unlock(&lock);
  }

private:
  pthread_cond_t cond;
  pthread_mutex_t lock;
  size_t cnt{0};
};

struct counts {
  size_t success{0};
  size_t failure{0};

  AtomicCounter cnt;

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

int test_entry_writer(awriter_entry_t* e) {
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

const awriter_callbacks_t test_callbacks = {
  &callback_counter,
  &test_entry_writer
};

}  // anonymous

// Simple test that creates a number of async writers and
// then destroys them.
TEST(awriter, create_destroy) {
  constexpr size_t kNumEntries = 20;
  std::unique_ptr<AsyncWriter> w[kNumEntries];

  for (size_t i = 0; i < kNumEntries; i++) {
    w[i] = folly::make_unique<AsyncWriter>(i);
  }
}

// Test that creates an async writer, writes a few things
// to it and checks that the writes complete with success
// and that the file is written to.
TEST(awriter, sanity) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  constexpr size_t kNumEntries = 3;
  testing_context_t e[kNumEntries];
  struct stat s;
  auto fd = std::make_shared<folly::File>(f.fd());

  auto w = folly::make_unique<AsyncWriter>();
  EXPECT_TRUE(w->start("awriter:test"));

  for (size_t i = 0; i < kNumEntries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.file = fd;
    e[i].log_context.buf = kWriteString.str();
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    EXPECT_TRUE(awriter_queue(w.get(), &e[i].log_context.awentry));
  }

  testCounter.cnt.wait([](size_t v) { return v >= kNumEntries; });

  w.reset();
  EXPECT_EQ(kNumEntries, testCounter.success);

  EXPECT_EQ(0, fstat(f.fd(), &s));

  EXPECT_EQ((int)(kNumEntries * kWriteString.size()), s.st_size);
}

// Test that ensures that pending items in the queue are
// flushed when the writer is stopped.
TEST(awriter, flush_queue) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  constexpr size_t kNumEntries = 10;
  testing_context_t e[kNumEntries];

  auto w = folly::make_unique<AsyncWriter>(0);

  for (size_t i = 0; i < kNumEntries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.buf = kWriteString.str();
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    EXPECT_TRUE(awriter_queue(w.get(), &e[i].log_context.awentry));
  }

  // Stop the writer even before we start the thread.
  w->stop();

  EXPECT_EQ(kNumEntries, testCounter.failure);
}

// Test that ensures that maximum queue length is honored.
TEST(awriter, max_queue_length) {
  counts testCounter;
  TemporaryFile f("awriter_test");
  constexpr size_t kMaxlen = 5;
  constexpr size_t kNumEntries = kMaxlen + 5;
  testing_context_t e[kNumEntries];
  auto fd = std::make_shared<folly::File>(f.fd());

  auto w = folly::make_unique<AsyncWriter>(kMaxlen);
  EXPECT_TRUE(w != nullptr);

  for (size_t i = 0; i < kNumEntries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.file = fd;
    e[i].log_context.buf = kWriteString.str();
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    bool ret = awriter_queue(w.get(), &e[i].log_context.awentry);
    if (i < kMaxlen) {
      EXPECT_TRUE(ret);
    } else {
      EXPECT_FALSE(ret);
    }
  }

  // Create the thread to process the requests and wait for all
  // of them to be completed.
  EXPECT_TRUE(w->start("awriter:test"));

  testCounter.cnt.wait([](size_t v) { return v >= kMaxlen; });

  EXPECT_EQ(kMaxlen, testCounter.success);

  // Make sure we can submit an entry again.
  EXPECT_TRUE(awriter_queue(w.get(), &e[0].log_context.awentry));
}

// Test that passes invalid fd and expect errors when writing.
TEST(awriter, invalid_fd) {
  counts testCounter;
  constexpr size_t kNumEntries = 3;
  testing_context_t e[kNumEntries];
  auto fd = std::make_shared<folly::File>(-1);

  auto w = folly::make_unique<AsyncWriter>(0);
  EXPECT_TRUE(w->start("awriter:test"));

  for (size_t i = 0; i < kNumEntries; i++) {
    e[i].counter = &testCounter;
    e[i].log_context.file = fd;
    e[i].log_context.buf = kWriteString.str();
    e[i].log_context.awentry.context = e + i;
    e[i].log_context.awentry.callbacks = &test_callbacks;
    EXPECT_TRUE(awriter_queue(w.get(), &e[i].log_context.awentry));
  }

  testCounter.cnt.wait([](size_t v) { return v >= kNumEntries; });

  w->stop();

  EXPECT_EQ(kNumEntries, testCounter.failure);
}
