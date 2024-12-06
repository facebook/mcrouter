# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_fbthrift_cpp_library(mcrouter_carbon_result_thrift carbon_result.thrift)

add_fbthrift_cpp_library(mcrouter_carbon_thrift carbon.thrift)

add_library(mcrouter_carbon_protocol CarbonQueueAppender.cpp
                                     CarbonProtocolReader.cpp Result.cpp)

target_link_libraries(
  mcrouter_carbon_protocol PUBLIC mcrouter_carbon_result_thrift Folly::folly
                                  FBThrift::thriftcpp2)

add_subdirectory(example)
add_subdirectory(test)
