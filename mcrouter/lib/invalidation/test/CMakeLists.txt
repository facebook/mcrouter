# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

file(GLOB mcrouter_invalidation_tests_sources CONFIGURE_DEPENDS "*.cpp")

add_executable(
  mcrouter_invalidation_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                              ${mcrouter_invalidation_tests_sources})

target_link_libraries(
  mcrouter_invalidation_tests PRIVATE mcrouter_invalidation mcrouterinternal
                                      Folly::folly GTest::gtest)
