/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/lib/carbon/RequestCommon.h"
#include <folly/io/async/Request.h>
#include <gtest/gtest.h>

using namespace ::testing;

using namespace carbon;

TEST(RequestCommonTest, PreprocessFunction) {
  RequestCommon req;
  EXPECT_TRUE(req.preprocessFunctionIsNull()); // null by default

  int x = 0;
  ASSERT_EQ(x, 0);
  req.setPreprocessFunction([&x]() { x = 1; });
  EXPECT_FALSE(req.preprocessFunctionIsNull()); // function set
  EXPECT_EQ(x, 0); // preprocess function not run yet

  req.runPreprocessFunction();
  EXPECT_FALSE(req.preprocessFunctionIsNull());
  EXPECT_EQ(x, 1); // run preprocess function
}

TEST(RequestCommonTest, RequestContextScopeGuard) {
  RequestCommon req;
  EXPECT_TRUE(req.requestContextScopeGuardIsNull()); // null by default

  auto guard = std::make_unique<folly::ShallowCopyRequestContextScopeGuard>();
  req.setRequestContextScopeGuard(std::move(guard));
  EXPECT_FALSE(req.requestContextScopeGuardIsNull()); // guard set

  req.destroyRequestContextScopeGuard();
  EXPECT_TRUE(req.requestContextScopeGuardIsNull()); // guard reset
}
