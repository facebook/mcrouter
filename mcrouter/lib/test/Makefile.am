check_PROGRAMS = mcrouter_lib_test

mcrouter_lib_test_SOURCES = \
  Ch3HashTest.cpp \
  Crc32HashTest.cpp \
  Main.cpp \
  MigrateRouteTest.cpp \
  RandomRouteTest.cpp \
  RequestReplyTest.cpp \
  RouteHandleTest.cpp \
  WeightedCh3HashFuncTest.cpp

mcrouter_lib_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_lib_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libgtest.la
