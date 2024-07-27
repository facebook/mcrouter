# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_fbthrift_cpp_library(
  mcrouter_carbon_test_thrift
  CarbonTest.thrift
  DEPENDS
  mcrouter_carbon_thrift
  mcrouter_carbon_result_thrift
  mcrouter_common_thrift
  OPTIONS
  "${THRIFT_OPTIONS}")

target_link_libraries(mcrouter_carbon_test_thrift
                      PUBLIC mcrouter_carbon_protocol)

add_library(mcrouter_carbon_test_messages CarbonTestMessages.cpp
                                          CarbonTestMessagesThrift.cpp)

target_link_libraries(mcrouter_carbon_test_messages
                      PUBLIC mcrouter_carbon_test_thrift)
