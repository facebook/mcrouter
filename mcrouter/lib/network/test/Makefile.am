check_PROGRAMS = mcrouter_network_test

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientTestSync.cpp \
  CarbonMessageDispatcherTest.cpp \
  CarbonMockMcTest.cpp \
  CarbonQueueAppenderTest.cpp \
  gen/CarbonTestMessages.cpp \
  McAsciiParserTest.cpp \
  McServerAsciiParserTest.cpp \
  MockMc.cpp \
  MockMcServer.cpp \
  SessionTest.cpp \
  SessionTestHarness.cpp \
  SessionTestHarness.h \
  TestClientServerUtil.cpp \
  TestClientServerUtil.h \
  TestMcAsciiParserUtil.cpp \
  TestMcAsciiParserUtil.h

mcrouter_network_test_CPPFLAGS = -I$(top_srcdir)/.. -isystem $(top_srcdir)/lib/gtest/include
mcrouter_network_test_LDADD = \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la \
  $(top_builddir)/lib/network/libtest_util.a
