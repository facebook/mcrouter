check_PROGRAMS = mcrouter_network_test

mcrouter_network_test_SOURCES = \
  AccessPointTest.cpp \
  AsyncMcClientTest.cpp

# TODO: enable when new thrift is pushed
#  SessionTest.cpp \
#  SessionTestHarness.cpp \
#  SessionTestHarness.h

mcrouter_network_test_CPPFLAGS = -I$(top_srcdir)/oss_include
mcrouter_network_test_LDADD = $(top_builddir)/lib/libmcrouter.a -lgtest -lgtestmain
