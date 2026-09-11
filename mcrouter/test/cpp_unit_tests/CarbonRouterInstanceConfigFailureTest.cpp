/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/Synchronized.h>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"
// defaultTestOptions(); its own header does not build when included directly.
#include "mcrouter/options.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

namespace {

constexpr folly::StringPiece kHandlerName = "config_failure_capture";

using Failure = std::pair<std::string, std::string>; // {category, message}

// Spins up a router with the given inline config and returns every failure
// logged while doing so. Handlers run on whichever thread logs, so the
// captured list is synchronized.
std::vector<Failure> failuresWhileConfiguring(std::string config) {
  folly::Synchronized<std::vector<Failure>> failures;
  failure::setHandler(
      {kHandlerName.str(),
       [&failures](
           folly::StringPiece,
           int,
           folly::StringPiece,
           folly::StringPiece category,
           folly::StringPiece msg,
           const std::map<std::string, std::string>&,
           bool) {
         failures.wlock()->emplace_back(category.str(), msg.str());
       }});
  SCOPE_EXIT {
    failure::removeHandler(kHandlerName);
  };

  McrouterOptions opts = defaultTestOptions();
  opts.config = std::move(config);
  try {
    CarbonRouterInstance<McrouterRouterInfo>::create(std::move(opts));
  } catch (const std::exception&) {
    // Expected for a config that cannot be preprocessed.
  }
  return *failures.rlock();
}

bool hasFailureStartingWith(
    const std::vector<Failure>& failures,
    folly::StringPiece category,
    folly::StringPiece messagePrefix) {
  for (const auto& [loggedCategory, message] : failures) {
    if (loggedCategory == category &&
        folly::StringPiece(message).startsWith(messagePrefix)) {
      return true;
    }
  }
  return false;
}

} // namespace

// An inline config is always readable, so a config that fails to preprocess
// must be reported only as a broken config. Reporting it as an unreadable
// config source as well sends whoever reads the failure log after the source
// mcrouter never had trouble reading.
TEST(CarbonRouterInstanceConfigFailureTest, UnparseableConfigIsNotUnreadable) {
  auto failures = failuresWhileConfiguring("{ \"route\": ");

  EXPECT_TRUE(hasFailureStartingWith(
      failures, failure::Category::kInvalidConfig, "Failed to reconfigure"))
      << "a config that cannot be preprocessed must be logged as a broken "
         "config";
  EXPECT_FALSE(hasFailureStartingWith(
      failures, failure::Category::kBadEnvironment, "Can not read config from"))
      << "the config was handed to mcrouter inline, so it cannot have been "
         "unreadable; only the preprocessing failure is real";
}

// The same failure must still be reported when the config source really is
// unreadable, which is the only case that line is about.
TEST(CarbonRouterInstanceConfigFailureTest, MissingConfigFileIsUnreadable) {
  auto failures = failuresWhileConfiguring("file:/dev/null/doesnotexist");

  EXPECT_TRUE(hasFailureStartingWith(
      failures,
      failure::Category::kBadEnvironment,
      "Can not read config from"));
}
