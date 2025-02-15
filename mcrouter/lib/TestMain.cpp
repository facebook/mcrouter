/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include <folly/init/Init.h>

/**
 * Main entry point shared by several test suites
 * in the mcrouter OSS build.
 */
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  folly::Init init(
      &argc, &argv, folly::InitOptions{}.removeFlags(false).useGFlags(false));

  return RUN_ALL_TESTS();
}
