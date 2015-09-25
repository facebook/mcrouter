check_PROGRAMS = mcrouter_routes_test

mcrouter_routes_test_SOURCES = \
  BigValueRouteTest.cpp \
  ConstShardHashFuncTest.cpp \
  FailoverWithExptimeRouteTest.cpp \
  Main.cpp \
  RateLimitRouteTest.cpp \
  RouteHandleTestUtil.cpp \
  RouteHandleTestUtil.h \
  ShadowRouteTest.cpp \
  SlowWarmUpRouteTest.cpp \
  WarmUpRouteTest.cpp

mcrouter_routes_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_routes_test_LDADD = \
  $(top_builddir)/libmcroutercore.a \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libgtest.la \
  -lfollybenchmark
