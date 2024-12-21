# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

check_PROGRAMS = mcrouter_lib_test

mcrouter_lib_test_SOURCES = \
  AllSyncCollectionRoute.h \
  Ch3HashTest.cpp \
  CompressionCodecManagerTest.cpp \
  CompressionTest.cpp \
  CompressionTestUtil.cpp \
  CompressionTestUtil.h \
  Crc32HashTest.cpp \
  DynamicUtilTest.cpp \
  FiberLocalTest.cpp \
  HashTestUtil.cpp \
  HashTestUtil.h \
  IovecCursorTest.cpp \
  Lz4ImmutableTest.cpp \
  Main.cpp \
  McResUtilTest.cpp \
  MigrateRouteTest.cpp \
  RandomRouteTest.cpp \
  RendezvousHashTest.cpp \
  RouteHandleTest.cpp \
  RouteHandleTestUtil.h \
  TestRouteHandle.h \
  WeightedCh3HashFuncTest.cpp \
  WeightedCh4HashFuncTest.cpp \
  WeightedChHashFuncBaseTest.cpp \
  WeightedRendezvousHashTest.cpp

mcrouter_lib_test_CPPFLAGS = \
  -I$(top_srcdir)/.. \
  -isystem $(top_srcdir)/lib/gtest/include

mcrouter_lib_test_LDADD = \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la \
  -lthriftcpp2 \
  -lthriftprotocol
