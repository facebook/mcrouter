# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

add_executable(
  mcpiper_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
  # McPiperVisitorTest.cpp
)

target_link_libraries(mcpiper_tests PRIVATE mcrouterinternal GTest::gtest
                                            Folly::folly)

gtest_discover_tests(mcpiper_tests)
