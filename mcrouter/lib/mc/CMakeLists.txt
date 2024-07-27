# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

file(GLOB mcrouter_mc_protocol_sources CONFIGURE_DEPENDS "*.cpp")

add_library(mcrouter_mc_protocol ${mcrouter_mc_protocol_sources})
