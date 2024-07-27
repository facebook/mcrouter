# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcrouter_fbi_sources CONFIGURE_DEPENDS "*.cpp" "*.c" "cpp/*.cpp")

add_library(mcrouter_fbi ${mcrouter_fbi_sources})

target_link_libraries(mcrouter_fbi PUBLIC Folly::folly)

add_subdirectory(cpp)
add_subdirectory(test)
