# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

file(GLOB cpp_unit_tests_sources CONFIGURE_DEPENDS "*.cpp")

add_executable(cpp_unit_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                              ${cpp_unit_tests_sources})

target_link_libraries(cpp_unit_tests GTest::gtest Folly::folly mcroutercore
                      mcrouterinternal mcrouter_hello_goodbye)

# Run these tests in the source tree so that they can easily load fixture files.
gtest_discover_tests(cpp_unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
