# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcrouter_config_sources CONFIGURE_DEPENDS "*.cpp")

add_library(mcrouter_config ${mcrouter_config_sources})

target_link_libraries(
  mcrouter_config
  PUBLIC Folly::folly
  PRIVATE mcrouter_fbi)

add_subdirectory(test)
