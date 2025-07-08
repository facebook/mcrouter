# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

add_executable(mcrouter_mock_mc_server MockMc.cpp MockMcServer.cpp)
set_property(TARGET mcrouter_mock_mc_server PROPERTY OUTPUT_NAME mock_mc_server)

target_link_libraries(
  mcrouter_mock_mc_server
  PRIVATE mcrouterinternal
          Folly::folly
          FBThrift::thriftcpp2
          FBThrift::async
          FBThrift::serverdbginfo
          FBThrift::transport
          FBThrift::thriftanyrep
          FBThrift::thrifttype
          FBThrift::thrifttyperep
          FBThrift::thriftprotocol
          FBThrift::rpcmetadata
          FBThrift::thriftannotation
          FBThrift::thriftmetadata
          FBThrift::concurrency
          FBThrift::runtime
          FBThrift::thrift-core
          atomic)

add_executable(mcrouter_mock_mc_thrift_server MockMc.cpp MockMcThriftServer.cpp)
set_property(TARGET mcrouter_mock_mc_thrift_server
             PROPERTY OUTPUT_NAME mock_mc_thrift_server)

target_link_libraries(
  mcrouter_mock_mc_thrift_server
  PRIVATE memcache_service_thrift
          mcrouterinternal
          Folly::folly
          FBThrift::thriftcpp2
          FBThrift::async
          FBThrift::serverdbginfo
          FBThrift::transport
          FBThrift::thriftanyrep
          FBThrift::thrifttype
          FBThrift::thrifttyperep
          FBThrift::thriftprotocol
          FBThrift::rpcmetadata
          FBThrift::thriftannotation
          FBThrift::thriftmetadata
          FBThrift::concurrency
          FBThrift::runtime
          FBThrift::thrift-core
          atomic)

add_executable(mcrouter_mock_mc_server_dual MockMc.cpp MockMcServerDual.cpp)
set_property(TARGET mcrouter_mock_mc_server_dual PROPERTY OUTPUT_NAME
                                                          mock_mc_server_dual)

target_link_libraries(
  mcrouter_mock_mc_server_dual
  PRIVATE memcache_service_thrift
          mcrouterinternal
          Folly::folly
          FBThrift::thriftcpp2
          FBThrift::async
          FBThrift::serverdbginfo
          FBThrift::transport
          FBThrift::thriftanyrep
          FBThrift::thrifttype
          FBThrift::thrifttyperep
          FBThrift::thriftprotocol
          FBThrift::rpcmetadata
          FBThrift::thriftannotation
          FBThrift::thriftmetadata
          FBThrift::concurrency
          FBThrift::runtime
          FBThrift::thrift-core
          atomic)

file(GLOB mcrouter_network_tests_sources CONFIGURE_DEPENDS "*Test.cpp")

add_library(mcrouter_network_tests_listen_socket ListenSocket.cpp)
target_link_libraries(mcrouter_network_tests_listen_socket PUBLIC Folly::folly)

add_executable(
  mcrouter_network_tests
  "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
  ${mcrouter_network_tests_sources}
  ClientSocket.cpp
  AsyncMcClientTestSync.cpp
  MockMc.cpp
  SessionTestHarness.cpp
  TestClientServerUtil.cpp
  TestMcAsciiParserUtil.cpp)

target_link_libraries(
  mcrouter_network_tests
  PRIVATE mcrouterinternal
          mcroutercore
          mcrouter_network_tests_gen
          mcrouter_network_tests_listen_socket
          mcrouter_hello_goodbye
          Folly::folly
          GTest::gtest
          FBThrift::thriftcpp2
          FBThrift::async
          FBThrift::serverdbginfo
          FBThrift::transport
          FBThrift::thriftanyrep
          FBThrift::thrifttype
          FBThrift::thrifttyperep
          FBThrift::thriftprotocol
          FBThrift::rpcmetadata
          FBThrift::thriftannotation
          FBThrift::thriftmetadata
          FBThrift::concurrency
          FBThrift::runtime
          FBThrift::thrift-core)

# Run these tests in the source tree so that they can easily load fixture files.
gtest_discover_tests(
  mcrouter_network_tests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  PROPERTIES
  TIMEOUT 30)

add_subdirectory(gen)
