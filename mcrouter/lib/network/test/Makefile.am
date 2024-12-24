# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

SUBDIRS = gen

noinst_PROGRAMS = mock_mc_server mock_mc_thrift_server mock_mc_server_dual
noinst_LIBRARIES = libtest_util.a
check_PROGRAMS = mcrouter_network_test

mock_mc_server_SOURCES = \
  MockMc.cpp \
  MockMc.h \
  MockMcOnRequest.h \
  MockMcServer.cpp
mock_mc_server_CPPFLAGS = -I$(top_srcdir)/..
# Link order matters.
mock_mc_server_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	-lthriftcpp2 \
	-lserverdbginfo \
	-ltransport \
	-lthriftanyrep \
	-lthrifttype \
	-lthrifttyperep \
	-lthriftprotocol \
	-lrpcmetadata \
	-lthriftmetadata \
	-lasync \
	-lconcurrency \
	-lruntime \
	-lthrift-core \
	-lfmt \
	-lfizz \
	-lwangle \
	-lfolly

mock_mc_thrift_server_SOURCES = \
  MockMc.cpp \
  MockMc.h \
  MockMcOnRequest.h \
  MockMcThriftServer.cpp
mock_mc_thrift_server_CPPFLAGS = -I$(top_srcdir)/..
# Link order matters.
mock_mc_thrift_server_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	-lthriftcpp2 \
	-lserverdbginfo \
	-ltransport \
	-lthriftanyrep \
	-lthrifttype \
	-lthrifttyperep \
	-lthriftprotocol \
	-lrpcmetadata \
	-lthriftmetadata \
	-lasync \
	-lconcurrency \
	-lruntime \
	-lthrift-core \
	-lfmt \
	-lfizz \
	-lwangle \
	-lfolly

mock_mc_server_dual_SOURCES = \
  MockMc.cpp \
  MockMc.h \
  MockMcOnRequest.h \
  MockMcServerDual.cpp
mock_mc_server_dual_CPPFLAGS = -I$(top_srcdir)/..
# Link order matters.
mock_mc_server_dual_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	-lthriftcpp2 \
	-lserverdbginfo \
	-ltransport \
	-lthriftanyrep \
	-lthrifttype \
	-lthrifttyperep \
	-lthriftprotocol \
	-lrpcmetadata \
	-lthriftmetadata \
	-lasync \
	-lconcurrency \
	-lruntime \
	-lthrift-core \
	-lfmt \
	-lfizz \
	-lwangle \
	-lfolly

libtest_util_a_SOURCES = \
  ClientSocket.cpp \
  ClientSocket.h \
  ListenSocket.cpp \
  ListenSocket.h
libtest_util_a_CPPFLAGS = -I$(top_srcdir)/..

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientTestSync.cpp \
  AsyncMcServerShutdownTest.cpp \
  AsyncMcServerTest.cpp \
  CarbonMessageDispatcherTest.cpp \
  CarbonMockMcTest.cpp \
  CarbonQueueAppenderTest.cpp \
  gen/gen-cpp2/CarbonTest_types.cpp \
  gen/gen-cpp2/CarbonTest_types.h \
  gen/gen-cpp2/CarbonTest_types.tcc \
  gen/gen-cpp2/CarbonTest_data.cpp \
  gen/gen-cpp2/CarbonTest_data.h \
  gen/CarbonTestMessages.cpp \
  gen/CarbonTestMessagesThrift.cpp \
  McAsciiParserTest.cpp \
  McParserTest.cpp \
  McServerAsciiParserTest.cpp \
  MemcacheConnectionTest.cpp \
  MemcacheTest.cpp \
  RequestExpiryTest.cpp \
  MockMc.cpp \
  SessionTest.cpp \
  SessionTestHarness.cpp \
  SessionTestHarness.h \
  TestClientServerUtil.cpp \
  TestClientServerUtil.h \
  TestMcAsciiParserUtil.cpp \
  TestMcAsciiParserUtil.h

mcrouter_network_test_CPPFLAGS = \
	-I$(top_srcdir)/.. \
	-isystem $(top_srcdir)/lib/gtest/include

mcrouter_network_test_LDADD = \
  $(top_builddir)/libmcroutercore.a \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/carbon/example/gen/libcarbon_hello_goodbye.a \
  $(top_builddir)/lib/libtestmain.la \
  $(top_builddir)/lib/network/test/libtest_util.a \
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
  -lfolly
