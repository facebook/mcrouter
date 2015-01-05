/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <condition_variable>
#include <mutex>
#include <string>

#include <gtest/gtest.h>

#include <folly/experimental/TestUtil.h>
#include <folly/FileUtil.h>

#include "mcrouter/FileObserver.h"
#include "mcrouter/PeriodicTaskScheduler.h"

using facebook::memcache::mcrouter::FileObserver;
using facebook::memcache::mcrouter::PeriodicTaskScheduler;
using folly::test::TemporaryFile;

const std::string BOGUS_CONFIG = "this/file/doesnot/exists";

TEST(FileObserver, sanity) {
  folly::test::TemporaryFile config("file_observer_test");
  std::string path(config.path().string());
  std::string contents = "a";
  std::mutex mut;
  std::condition_variable cv;

  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());

  PeriodicTaskScheduler ts;
  int counter = 0;
  FileObserver::startObserving(path, ts, 100, 500,
                               [&counter, &cv] (std::string) {
                                  counter++;
                                  cv.notify_all();
                               });

  EXPECT_EQ(counter, 1);
  contents = "b";
  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());
  {
    std::unique_lock<std::mutex> lock(mut);
    cv.wait_for(lock, std::chrono::seconds(5));
  }
  EXPECT_EQ(counter, 2);
  contents = "c";
  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());
  {
    std::unique_lock<std::mutex> lock(mut);
    cv.wait_for(lock, std::chrono::seconds(5));
  }
  EXPECT_EQ(counter, 3);

  ts.shutdownAllTasks();
}

TEST(FileObserver, on_error_callback) {
  PeriodicTaskScheduler ts;
  int successCounter1 = 0, errorCounter1 = 0;
  FileObserver::startObserving(
    BOGUS_CONFIG, ts, 100, 500,
    [&successCounter1] (std::string) { successCounter1++; },
    [&errorCounter1] () { errorCounter1++; });

  int successCounter2 = 0, errorCounter2 = 0;
  FileObserver::startObserving(
    "", ts, 100, 500,
    [&successCounter2] (std::string) { successCounter2++; },
    [&errorCounter2] () { errorCounter2++; });

  EXPECT_EQ(successCounter1, 0);
  EXPECT_EQ(errorCounter1, 1);

  EXPECT_EQ(successCounter2, 0);
  EXPECT_EQ(errorCounter2, 1);

  ts.shutdownAllTasks();
}
