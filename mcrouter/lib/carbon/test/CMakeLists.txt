# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

add_executable(
  mcrouter_carbon_tests
  "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
  CarbonMessageConversionUtilsTest.cpp
  # TODO: These tests do not compile because the required Thrift clients cannot
  # be generated.
  # CarbonStructuresTest.cpp
  JsonClientTest.cpp
  Util.cpp
  RequestReplyUtilTest.cpp
  SerializationTest.cpp
  Timestamp.cpp
  VariantTest.cpp)

target_link_libraries(
  mcrouter_carbon_tests
  PRIVATE mcrouter_carbon_protocol
          mcrouter_carbon_test_thrift
          mcrouter_carbon_test_messages
          mcrouter_network_tests_listen_socket
          mcrouterinternal
          GTest::gtest
          Folly::folly
          atomic)

# Run these tests in the source tree so that they can easily load fixture files.
gtest_discover_tests(mcrouter_carbon_tests
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_subdirectory(gen)
