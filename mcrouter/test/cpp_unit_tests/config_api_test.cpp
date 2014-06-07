#include <string>

#include <gtest/gtest.h>

#include "folly/FileUtil.h"
#include "folly/experimental/TestUtil.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/options.h"

using folly::test::TemporaryFile;
using facebook::memcache::McrouterOptions;
using facebook::memcache::mcrouter::ConfigApi;
using facebook::memcache::mcrouter::ConfigType;

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

  std::string buf;
  EXPECT_TRUE(api.get(ConfigType::ConfigFile, path, buf));
  EXPECT_EQ(contents, buf);

  EXPECT_TRUE(api.getConfigFile(buf));
  EXPECT_EQ(contents, buf);

  EXPECT_EQ(changes, 0);

  contents = "b";
  EXPECT_EQ(folly::writeFull(config.fd(), contents.data(), contents.size()),
            contents.size());

  // wait for file to flush and api to check for update
  sleep(4);

  EXPECT_EQ(changes, 1);

  EXPECT_TRUE(api.getConfigFile(buf));
  EXPECT_EQ("ab", buf);

  EXPECT_TRUE(api.get(ConfigType::ConfigFile, path, buf));
  EXPECT_EQ("ab", buf);
  api.stopObserving(getpid());
}
