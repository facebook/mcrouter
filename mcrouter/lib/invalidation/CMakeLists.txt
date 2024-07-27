# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcrouter_invalidation_sources CONFIGURE_DEPENDS "*.cpp")

add_library(mcrouter_invalidation ${mcrouter_invalidation_sources})

target_link_libraries(mcrouter_invalidation PUBLIC Folly::folly
                                                   FBThrift::thriftcpp2)

add_subdirectory(test)
