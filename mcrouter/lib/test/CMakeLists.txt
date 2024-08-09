# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

file(GLOB mcrouter_lib_tests_sources CONFIGURE_DEPENDS "*.cpp")
list(REMOVE_ITEM mcrouter_lib_tests_sources
     ${CMAKE_CURRENT_SOURCE_DIR}/WeightedHashBenchmark.cpp)

add_executable(mcrouter_lib_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                                  ${mcrouter_lib_tests_sources})

target_link_libraries(
  mcrouter_lib_tests PRIVATE mcrouterinternal GTest::gtest Folly::folly
                             Folly::follybenchmark FBThrift::thriftcpp2)

add_executable(mcrouter_weighted_hash_benchmark WeightedHashBenchmark.cpp)

target_link_libraries(
  mcrouter_weighted_hash_benchmark PRIVATE mcrouter_hash_functions Folly::folly
                                           Folly::follybenchmark)

gtest_discover_tests(mcrouter_lib_tests)
