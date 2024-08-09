# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_fbthrift_cpp_library(
  mcrouter_hello_goodbye_thrift
  HelloGoodbye.thrift
  DEPENDS
  mcrouter_carbon_thrift
  mcrouter_carbon_result_thrift
  mcrouter_common_thrift
  OPTIONS
  "${THRIFT_OPTIONS}")

add_fbthrift_cpp_library(
  mcrouter_hello_goodbye_service_thrift
  HelloGoodbyeService.thrift
  DEPENDS
  mcrouter_hello_goodbye_thrift
  SERVICES
  HelloGoodbye
  OPTIONS
  "${THRIFT_OPTIONS}")

add_library(mcrouter_hello_goodbye_messages HelloGoodbyeMessages.cpp
                                            HelloGoodbyeMessagesThrift.cpp)

target_link_libraries(mcrouter_hello_goodbye_messages
                      PUBLIC mcrouter_hello_goodbye_thrift)

add_library(
  mcrouter_hello_goodbye
  HelloGoodbyeRouterInfo.cpp HelloGoodbyeRouterInfo-BuildExtraProvider.cpp
  HelloGoodbyeRouterInfo-ExternTemplate.cpp)

add_dependencies(mcrouter_hello_goodbye mcrouter_symlink_headers)

target_link_libraries(
  mcrouter_hello_goodbye
  PUBLIC mcrouter_hello_goodbye_service_thrift mcrouter_hello_goodbye_messages
  PRIVATE mcrouterinternal Folly::folly FBThrift::thriftcpp2)

add_executable(mcrouter_hello_goodbye_client_tool HelloGoodbyeClientTool.cpp)

target_link_libraries(
  mcrouter_hello_goodbye_client_tool PRIVATE mcrouter_hello_goodbye_messages
                                             mcrouterinternal Folly::folly)
