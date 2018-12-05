/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include <string>

#include <gtest/gtest.h>

#include "mcrouter/lib/McResUtil.h"

namespace facebook {
namespace memcache {
namespace test {

TEST(McResUtil, mc_res_from_string) {
  const char* resStr1 = "mc_res_busy";
  ASSERT_EQ(mc_res_from_string(resStr1), mc_res_busy);

  const char* resStr2 = "bad_string";
  ASSERT_EQ(mc_res_from_string(resStr2), mc_res_unknown);

  std::string resStr3 = "mc_res_notfound";
  ASSERT_EQ(mc_res_from_string(resStr3.c_str()), mc_res_notfound);
}

} // namespace test
} // namespace memcache
} // namespace facebook
