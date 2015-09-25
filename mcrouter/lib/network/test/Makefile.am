check_PROGRAMS = mcrouter_network_test

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientTestSync.cpp \
  SessionTest.cpp \
  SessionTestHarness.cpp \
  SessionTestHarness.h \
  TestClientServerUtil.h

mcrouter_network_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_network_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libgtestmain.la
