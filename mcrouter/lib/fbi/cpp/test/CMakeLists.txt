# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

add_executable(
  mcrouter_fbi_cpp_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                         LowerBoundPrefixMapTest.cpp ObjectPoolTests.cpp)

target_link_libraries(mcrouter_fbi_cpp_tests PRIVATE mcrouter_fbi GTest::gtest
                                                     Folly::folly)

add_executable(mcrouter_fbi_cpp_benchmarks FuncGeneratorBenchmark.cpp)

target_link_libraries(mcrouter_fbi_cpp_benchmarks
                      PRIVATE mcrouter_fbi Folly::folly Folly::follybenchmark)

gtest_discover_tests(mcrouter_fbi_cpp_tests)
