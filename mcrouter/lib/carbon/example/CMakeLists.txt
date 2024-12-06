# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_subdirectory(gen)

if(BUILD_TESTS)
  add_executable(
    mcrouter_hello_goodbye_tests "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
                                 HelloGoodbyeTest.cpp)

  target_link_libraries(
    mcrouter_hello_goodbye_tests
    PRIVATE mcrouter_hello_goodbye_messages
            mcrouter_hello_goodbye_service_thrift mcrouterinternal Folly::folly
            GTest::gtest)

  gtest_discover_tests(mcrouter_hello_goodbye_tests)
endif()
