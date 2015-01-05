/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>

#include <gtest/gtest.h>

#include <folly/experimental/TestUtil.h>
#include <folly/FileUtil.h>

#include "mcrouter/ConfigApi.h"
#include "mcrouter/options.h"

using facebook::memcache::McrouterOptions;
using facebook::memcache::mcrouter::ConfigApi;
using facebook::memcache::mcrouter::ConfigType;
using folly::test::TemporaryFile;

TEST(ConfigApi, file_change) {
  TemporaryFile config("config_api_test");
  std::string contents = "a";
  std::string path(config.path().string());

  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());

  McrouterOptions opts;
  opts.config_file = path;
  ConfigApi api(opts);
  api.startObserving();

  std::atomic<int> changes(0);
  auto handle = api.subscribe([&changes]() { ++changes; });

  api.trackConfigSources();
  std::string buf;
  EXPECT_TRUE(api.get(ConfigType::ConfigFile, path, buf));
  EXPECT_EQ(contents, buf);

  EXPECT_TRUE(api.getConfigFile(buf));
  EXPECT_EQ(contents, buf);
  api.subscribeToTrackedSources();

  EXPECT_EQ(changes, 0);

  contents = "b";
  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());

  // wait for the file to flush and api to check for update
  sleep(4);

  EXPECT_EQ(changes, 1);

  EXPECT_TRUE(api.getConfigFile(buf));
  EXPECT_EQ("ab", buf);

  EXPECT_TRUE(api.get(ConfigType::ConfigFile, path, buf));
  EXPECT_EQ("ab", buf);

  // clear tracked sources
  api.trackConfigSources();
  api.subscribeToTrackedSources();

  contents = "c";
  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());

  // wait for the file to flush
  sleep(4);

  EXPECT_EQ(changes, 1);

  EXPECT_TRUE(api.getConfigFile(buf));
  EXPECT_EQ("abc", buf);

  EXPECT_TRUE(api.get(ConfigType::ConfigFile, path, buf));
  EXPECT_EQ("abc", buf);

  api.stopObserving(getpid());
}
