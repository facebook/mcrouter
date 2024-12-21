# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

check_PROGRAMS = mcrouter_routes_test

mcrouter_routes_test_SOURCES = \
  AllSyncCollectionRouteFactory.h \
  BigValueRouteTestBase.h \
  BigValueRouteTest.cpp \
  ConstShardHashFuncTest.cpp \
  DistributionRouteTest.cpp \
  FailoverRouteTest.cpp \
  FailoverWithExptimeRouteTest.cpp \
  KeyParseRouteTest.cpp \
  LatestRouteTest.cpp \
  LoadBalancerRouteTest.cpp \
  LoggingRouteTest.cpp \
  Main.cpp \
  McBucketRouteTest.cpp \
  McRefillRouteTest.cpp \
  MissFailoverRouteTest.cpp \
  OriginalClientHashRouteTest.cpp \
  OutstandingLimitRouteTest.cpp \
  PrefixSelectorRouteTest.cpp \
  RateLimitRouteTest.cpp \
  RootRouteTest.cpp \
  RouteHandleTestBase.h \
  RouteHandleTestUtil.h \
  RoutePolicyMapTest.cpp \
  ShadowRouteTest.cpp \
  ShadowSettingsTest.cpp \
  ShardSplitRouteTest.cpp \
  ShardSplitRouteTestUtil.h \
  ShardSplitterTest.cpp \
  SlowWarmUpRouteTest.cpp \
  WarmUpRouteTest.cpp

# The following tests depend on HelloGoodbyeService
# and thus need to be disabled until mcrouter OSS depends on fb303.
#
# HashStopAllowListRouteTest.cpp
# KeySplitRouteTest.cpp
# ErrorRouteTest.cpp
# EagerShardSelectionRouteTest.cpp
# EagerShardSelectionShadowRouteTest.cpp
# CollectionRouteFactoryTest.cpp
# ClientCompatibilityCheckerRouteTest.cpp
# LatencyInjectionRouteTest.cpp
# BlackholeRouteTest.cpp
# ShardSelectionRouteTest.cpp
# StagingRouteTest.cpp

mcrouter_routes_test_CPPFLAGS = \
 -I$(top_srcdir)/.. \
 -isystem $(top_srcdir)/lib/gtest/include

mcrouter_routes_test_LDADD = \
  $(top_builddir)/libmcroutercore.a \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la \
  -lthriftcpp2 \
  -lserverdbginfo \
  -ltransport \
  -lthriftanyrep \
  -lthrifttype \
  -lthrifttyperep \
  -lthriftprotocol \
  -lrpcmetadata \
  -lthriftannotation \
  -lthriftmetadata \
  -lasync \
  -lconcurrency \
  -lruntime \
  -lthrift-core \
  -lwangle \
  -lfizz \
  -lsodium \
  -lfolly \
  -lfollybenchmark
