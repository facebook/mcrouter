/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <getopt.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/StandaloneUtils.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

namespace {

CmdLineOptions parse(std::vector<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  // glibc: 0 (not 1) fully reinitializes getopt_long between parses.
  optind = 0;
  return parseCmdLineOptions(static_cast<int>(argv.size()), argv.data());
}

struct BuiltOptions {
  McrouterOptions opts;
  McrouterStandaloneOptions standaloneOpts;
};

BuiltOptions build(const std::string& serviceName, const std::string& port) {
  auto cmdLineOpts =
      parse({"mcrouter", "--port", port, "--config", "file:/dev/null"});
  std::unordered_map<std::string, std::string> optionsDict;
  std::unordered_map<std::string, std::string> standaloneOptionsDict;
  getFlavorOptionsAndApplyOverrides(
      cmdLineOpts, optionsDict, standaloneOptionsDict);

  BuiltOptions res;
  buildStandaloneOptions(
      serviceName,
      cmdLineOpts,
      optionsDict,
      standaloneOptionsDict,
      res.opts,
      res.standaloneOpts);
  return res;
}

} // namespace

TEST(StandaloneUtilsTest, BuildStandaloneOptionsIsRepeatablePerRouter) {
  initStandaloneProcessEarly(
      parse({"mcrouter", "--port", "5000", "--config", "file:/dev/null"}), {});

  auto a = build("svcA", "5001");
  auto b = build("svcB", "5002");

  EXPECT_EQ("svcA", a.opts.service_name);
  EXPECT_EQ("5001", a.opts.router_name);
  EXPECT_EQ(std::vector<uint16_t>{5001}, a.standaloneOpts.ports);

  EXPECT_EQ("svcB", b.opts.service_name);
  EXPECT_EQ("5002", b.opts.router_name);
  EXPECT_EQ(std::vector<uint16_t>{5002}, b.standaloneOpts.ports);
}
