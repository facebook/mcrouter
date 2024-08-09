# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE file
# in the root directory of this source tree.

if(NOT BUILD_TESTS)
  return()
endif()

add_executable(
  mcrouter_routes_tests
  "${CMAKE_SOURCE_DIR}/mcrouter/test/main.cpp"
  BigValueRouteTest.cpp
  BlackholeRouteTest.cpp
  ClientCompatibilityCheckerRouteTest.cpp
  CollectionRouteFactoryTest.cpp
  ConstShardHashFuncTest.cpp
  DistributionRouteTest.cpp
  EagerShardSelectionRouteTest.cpp
  EagerShardSelectionShadowRouteTest.cpp
  ErrorRouteTest.cpp
  FailoverRouteTest.cpp
  FailoverWithExptimeRouteTest.cpp
  HashStopAllowListRouteTest.cpp
  KeyParseRouteTest.cpp
  KeySplitRouteTest.cpp
  LatencyInjectionRouteTest.cpp
  LatestRouteTest.cpp
  LoadBalancerRouteTest.cpp
  McBucketRouteTest.cpp
  McRefillRouteTest.cpp
  MissFailoverRouteTest.cpp
  OriginalClientHashRouteTest.cpp
  OutstandingLimitRouteTest.cpp
  PrefixSelectorRouteTest.cpp
  RateLimitRouteTest.cpp
  RootRouteTest.cpp
  RoutePolicyMapTest.cpp
  ShadowRouteTest.cpp
  ShadowSettingsTest.cpp
  ShardSelectionRouteTest.cpp
  ShardSplitRouteTest.cpp
  ShardSplitterTest.cpp
  SlowWarmUpRouteTest.cpp
  StagingRouteTest.cpp
  WarmUpRouteTest.cpp)

target_link_libraries(
  mcrouter_routes_tests PRIVATE mcrouter_hello_goodbye mcroutercore
                                GTest::gtest Folly::folly FBThrift::thriftcpp2)

gtest_discover_tests(mcrouter_routes_tests)
