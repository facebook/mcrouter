# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcpiper_sources CONFIGURE_DEPENDS "*.cpp")

add_executable(mcpiper ${mcpiper_sources})

target_link_libraries(
  mcpiper
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
  FBThrift::thriftmetadata
  FBThrift::concurrency
  FBThrift::thrift-core
  fmt::fmt
  wangle::wangle)

add_subdirectory(test)
