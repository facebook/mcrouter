# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_fbthrift_cpp_library(
  mcrouter_network_carbon_test_thrift
  CarbonTest.thrift
  DEPENDS
  mcrouter_carbon_result_thrift
  mcrouter_carbon_thrift
  OPTIONS
  "${THRIFT_OPTIONS}")

target_link_libraries(mcrouter_network_carbon_test_thrift
                      PUBLIC mcrouter_carbon_protocol)

file(GLOB mcrouter_network_tests_gen_sources CONFIGURE_DEPENDS "*.cpp")

add_library(mcrouter_network_tests_gen ${mcrouter_network_tests_gen_sources})

target_link_libraries(
  mcrouter_network_tests_gen PUBLIC mcrouter_network_carbon_test_thrift
                                    mcrouter_carbon_protocol)
