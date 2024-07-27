# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

file(GLOB mcrouter_config_tests_sources CONFIGURE_DEPENDS "*.cpp")

add_executable(
  mcrouter_config_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                        ${mcrouter_config_tests_sources})

target_link_libraries(mcrouter_config_tests PRIVATE mcrouter_config
                                                    GTest::gtest Folly::folly)

# Run these tests in the source tree so that they can easily load fixture files.
gtest_discover_tests(mcrouter_config_tests
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
