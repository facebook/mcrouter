check_PROGRAMS = mcrouter_routes_test

mcrouter_routes_test_SOURCES = \
  BigValueRouteTest.cpp \
  ConstShardHashFuncTest.cpp \
  FailoverWithExptimeRouteTest.cpp \
  Main.cpp \
  RateLimitRouteTest.cpp \
  ShadowRouteTest.cpp \
  WarmUpRouteTest.cpp

mcrouter_routes_test_CPPFLAGS = -I$(top_srcdir)/oss_include
mcrouter_routes_test_LDADD = $(top_builddir)/libmcroutercore.a $(top_builddir)/lib/libmcrouter.a -lgtest -lfollybenchmark
