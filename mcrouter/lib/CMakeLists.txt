# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

add_library(mcrouter_clocks Clocks.cpp)

add_library(
  mcrouter_hash_functions
  RendezvousHashFunc.cpp RendezvousHashHelper.cpp WeightedCh3HashFunc.cpp
  WeightedCh4HashFunc.cpp WeightedChHashFuncBase.cpp
  WeightedRendezvousHashFunc.cpp)

target_link_libraries(
  mcrouter_hash_functions
  PUBLIC Folly::folly
  PRIVATE mcrouter_fbi)

add_library(mcrouter_utils DynamicUtil.cpp IOBufUtil.cpp)

target_link_libraries(mcrouter_utils PUBLIC Folly::folly FBThrift::thriftcpp2)

add_library(
  mcrouter_compression_codecs
  Compression.cpp
  CompressionCodecManager.cpp
  IovecCursor.cpp
  Lz4CompressionCodec.cpp
  Lz4Immutable.cpp
  Lz4ImmutableCompressionCodec.cpp
  ZstdCompressionCodec.cpp)

target_link_libraries(
  mcrouter_compression_codecs
  PUBLIC Folly::folly
  PRIVATE mcrouter_utils)

add_library(mcrouter_thread_pools AuxiliaryCPUThreadPool.cpp
                                  AuxiliaryIOThreadPool.cpp)

target_link_libraries(mcrouter_thread_pools PUBLIC Folly::folly)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/network/McAsciiParser-gen.cpp
  COMMAND ragel -G1 ${CMAKE_CURRENT_SOURCE_DIR}/network/McAsciiParser.rl -o
          ${CMAKE_CURRENT_BINARY_DIR}/network/McAsciiParser-gen.cpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/network/McAsciiParser.rl
  VERBATIM)

file(GLOB libmcrouter_network_sources CONFIGURE_DEPENDS "network/*.cpp")

add_library(
  mcrouterinternal
  FailoverErrorsSettingsBase.cpp
  FiberLocalInternal.cpp
  MessageQueue.cpp
  StatsReply.cpp
  carbon/CmdLineClient.cpp
  carbon/JsonClient.cpp
  carbon/connection/ExternalCarbonConnectionImpl.cpp
  ${libmcrouter_network_sources}
  network/McAsciiParser-gen.cpp
  network/gen/CommonMessages.cpp
  network/gen/CommonMessagesThrift.cpp
  network/gen/MemcacheMessages.cpp
  network/gen/MemcacheMessagesThrift.cpp
  network/gen/MemcacheRouterInfo-AllFastestRoute.cpp
  network/gen/MemcacheRouterInfo-BuildExtraProvider.cpp
  network/gen/MemcacheRouterInfo-ExternTemplate.cpp
  network/gen/MemcacheRouterInfo-FailoverRoute.cpp
  network/gen/MemcacheRouterInfo-HashRoute.cpp
  network/gen/MemcacheRouterInfo.cpp)

set_property(TARGET mcrouterinternal PROPERTY OUTPUT_NAME mcrouter)

target_link_libraries(
  mcrouterinternal
  PUBLIC mcrouter_fbi
         mcrouter_config
         mcrouter_debug
         mcrouter_hash_functions
         mcrouter_compression_codecs
         mcrouter_thread_pools
         mcrouter_carbon_protocol
         mcrouter_mc_protocol
         mcrouter_carbon_result_thrift
         mcrouter_carbon_thrift
         mcrouter_common_thrift
         mcrouter_memcache_thrift
         memcache_service_thrift
  PRIVATE mcrouter_clocks)

add_subdirectory(carbon)
add_subdirectory(config)
add_subdirectory(debug)
add_subdirectory(fbi)
add_subdirectory(mc)
add_subdirectory(network)
add_subdirectory(invalidation)
add_subdirectory(test)
