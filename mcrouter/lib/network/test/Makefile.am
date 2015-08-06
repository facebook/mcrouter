check_PROGRAMS = mcrouter_network_test

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientTest.cpp \
  SessionTest.cpp \
  SessionTestHarness.cpp \
  SessionTestHarness.h

mcrouter_network_test_CPPFLAGS = -I$(top_srcdir)/..
mcrouter_network_test_LDADD = $(top_builddir)/lib/libmcrouter.a -lgtest -lgtestmain
