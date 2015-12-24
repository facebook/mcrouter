check_PROGRAMS = mcrouter_network_test

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientCaretTest.cpp \
  AsyncMcClientTestSync.cpp \
  McAsciiParserTest.cpp \
  McServerAsciiParserTest.cpp \
  SessionTest.cpp \
  SessionTestHarness.cpp \
  SessionTestHarness.h \
  TestClientServerUtil.cpp \
  TestClientServerUtil.h \
  TestMcAsciiParserUtil.cpp \
  TestMcAsciiParserUtil.h \
  ThriftMockMcTest.cpp \
  ThriftMsgDispatcherTest.cpp

mcrouter_network_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_network_test_LDADD = \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/network/libtest_util.a \
  $(top_builddir)/lib/libgtestmain.la
