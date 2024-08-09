# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcrouter_fbi_cpp_sources CONFIGURE_DEPENDS "*.cpp")

add_library(mcrouter_fbi_cpp ${mcrouter_fbi_cpp_sources})

target_link_libraries(mcrouter_fbi_cpp PUBLIC Folly::folly)

add_subdirectory(test)
