# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

cmake_minimum_required(VERSION 3.19)

project(mcrouter VERSION 1.0.0)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/build/fbcode_builder/CMake"
                      ${CMAKE_MODULE_PATH})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

option(BUILD_TESTS "If enabled, compile the tests." OFF)
option(USE_CCACHE "Use ccache for compiler caching, if available" OFF)
option(PROFILE_BUILD "Profile build times (requires clang)" OFF)

# Opt out of using the upstream BoostConfig module with CMake >= 3.30,
# since GitHub Actions provides Boost < 1.70 and therefore is missing the upstream module.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

# Disable Meta-specific functionality for the OSS build.
add_compile_definitions(LIBMC_FBTRACE_DISABLE DISABLE_COMPRESSION
                        MCROUTER_OSS_BUILD)

# Allow using a compiler cache locally on an opt-in basis.
if(USE_CCACHE)
  find_program(CCACHE_EXECUTABLE ccache)
  if(CCACHE_EXECUTABLE)
    set(ccacheEnv CCACHE_SLOPPINESS=pch_defines,time_macros)

    foreach(lang IN ITEMS C CXX)
      set(CMAKE_${lang}_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${ccacheEnv}
                                          ${CCACHE_EXECUTABLE})
    endforeach()
  endif()
endif()

# Allow profiling build performance with clang.
if(PROFILE_BUILD)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-ftime-trace>)
    message("Enabling build time profiling")
  else()
    message(
      "${CMAKE_CXX_COMPILER_ID} compiler detected, cannot enable build time profiling"
    )
  endif()
endif()

if(WIN32)
  include(FBCompilerSettingsMSVC)
else()
  include(FBCompilerSettingsUnix)

  # Create symlink to compile_commands.json for IDE to pick it up
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -E create_symlink
      ${CMAKE_BINARY_DIR}/compile_commands.json
      ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json)
endif()

# Set up options for generated Thrift client code
include(FBThriftLibrary)
set(THRIFT_OPTIONS stack_arguments sync_methods_return_try
                   deprecated_terse_writes)

find_package(Boost 1.65.1 REQUIRED COMPONENTS system thread filesystem regex
                                              context program_options)

find_package(fmt REQUIRED)
find_package(folly REQUIRED)
find_package(Fizz REQUIRED)
find_package(Glog REQUIRED)
find_package(gflags REQUIRED)
find_package(wangle REQUIRED)
find_package(FBThrift REQUIRED)

include_directories(.)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(BUILD_TESTS)
  enable_testing()
  include(CTest)

  find_package(GTest MODULE REQUIRED)

  include(GoogleTest)
endif()

add_subdirectory(mcrouter)

install(TARGETS mcrouter mcpiper)

# Provide a basic CPack integration to allow easily creating packages for Linux
# distributions.
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "mcrouter - a memcached protocol router for scaling memcached deployments.")
set(CPACK_PACKAGE_VENDOR "Meta Platforms, Inc.")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
