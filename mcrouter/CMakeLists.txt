# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/RouterRegistry.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/RouterRegistry-impl.h"
  COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/RouterRegistry-impl.h"
    "${CMAKE_CURRENT_BINARY_DIR}/RouterRegistry.h"
  COMMENT "Creating RouterRegistry.h symlink")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ThriftAcceptor.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ThriftAcceptor-impl.h"
  COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/ThriftAcceptor-impl.h"
    "${CMAKE_CURRENT_BINARY_DIR}/ThriftAcceptor.h"
  COMMENT "Creating ThriftAcceptor.h symlink")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/HostWithShard-fwd.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/HostWithShard-fwd-impl.h"
  COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/HostWithShard-fwd-impl.h"
    "${CMAKE_CURRENT_BINARY_DIR}/HostWithShard-fwd.h"
  COMMENT "Creating HostWithShard-fwd.h symlink")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mcrouter_sr_deps.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mcrouter_sr_deps-impl.h"
  COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/mcrouter_sr_deps-impl.h"
    "${CMAKE_CURRENT_BINARY_DIR}/mcrouter_sr_deps.h"
  COMMENT "Creating mcrouter_sr_deps.h symlink")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/config-impl.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mcrouter_config-impl.h"
  COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/mcrouter_config-impl.h"
    "${CMAKE_CURRENT_BINARY_DIR}/config-impl.h"
  COMMENT "Creating config-impl.h symlink")

add_custom_target(
  mcrouter_symlink_headers
  DEPENDS RouterRegistry.h ThriftAcceptor.h HostWithShard-fwd.h
          mcrouter_sr_deps.h config-impl.h)

list(
  APPEND
  mcrouter_executable_sources
  "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/RequestAclChecker.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/StandaloneConfig.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/StandaloneUtils.cpp")

file(GLOB mcrouter_core_sources CONFIGURE_DEPENDS "*.cpp" "routes/*.cpp")
list(REMOVE_ITEM mcrouter_core_sources ${mcrouter_executable_sources})

add_library(mcroutercore ${mcrouter_core_sources})

add_dependencies(mcroutercore mcrouter_symlink_headers)

target_link_libraries(
  mcroutercore
  PUBLIC mcrouter_carbon_result_thrift mcrouter_carbon_thrift
         mcrouter_common_thrift mcrouter_memcache_thrift memcache_service_thrift
         Folly::folly
  PRIVATE mcrouter_invalidation FBThrift::thriftcpp2 atomic)

add_executable(mcrouter ${mcrouter_executable_sources})

target_link_libraries(
  mcrouter
  mcroutercore
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
  fmt::fmt
  wangle::wangle
  pthread
  atomic)

add_subdirectory(lib)
add_subdirectory(routes)
add_subdirectory(tools)
add_subdirectory(test)
