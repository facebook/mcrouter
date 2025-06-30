# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

file(GLOB mcrouter_routes_tests_sources CONFIGURE_DEPENDS "*Test.cpp")

add_executable(
  mcrouter_routes_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                        ${mcrouter_routes_tests_sources})

target_link_libraries(
  mcrouter_routes_tests PRIVATE mcrouter_hello_goodbye mcroutercore
                                GTest::gtest Folly::folly FBThrift::thriftcpp2)

add_executable(shard_selection_route_factory_benchmark
               ShardSelectionRouteFactoryBenchmark.cpp)

target_link_libraries(
  shard_selection_route_factory_benchmark
  PRIVATE mcrouter_hello_goodbye mcrouter_fbi mcroutercore Folly::folly
          Folly::follybenchmark FBThrift::thriftcpp2)

# The default constexpr loop limit with GCC is too low to handle the constant
# expressions within this benchmark.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(
    shard_selection_route_factory_benchmark
    PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fconstexpr-loop-limit=8388608>)
endif()

gtest_discover_tests(mcrouter_routes_tests)
