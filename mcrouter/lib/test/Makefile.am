check_PROGRAMS = mcrouter_lib_test

mcrouter_lib_test_SOURCES = \
  Ch3HashTest.cpp \
  Crc32HashTest.cpp \
  FailoverRouteTest.cpp \
  LatestRouteTest.cpp \
  Main.cpp \
  MigrateRouteTest.cpp \
  MissFailoverRouteTest.cpp \
  RequestReplyTest.cpp \
  RouteHandleTest.cpp \
  WarmUpRouteTest.cpp \
  WeightedCh3HashFuncTest.cpp

mcrouter_lib_test_CPPFLAGS = -I$(top_srcdir)/oss_include
mcrouter_lib_test_LDADD = $(top_builddir)/lib/libmcrouter.a -lgtest
