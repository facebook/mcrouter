# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcrouter_debug_sources CONFIGURE_DEPENDS "*.cpp")

add_library(mcrouter_debug ${mcrouter_debug_sources})

target_link_libraries(
  mcrouter_debug
  PUBLIC Folly::folly
  PRIVATE mcrouter_fbi)
