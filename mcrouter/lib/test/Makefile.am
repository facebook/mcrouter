check_PROGRAMS = mcrouter_lib_test

mcrouter_lib_test_SOURCES = \
  Ch3HashTest.cpp \
  CompressionTest.cpp \
  CompressionTestUtil.cpp \
  CompressionTestUtil.h \
  Crc32HashTest.cpp \
  MigrateRouteTest.cpp \
  RandomRouteTest.cpp \
  RendezvousHashTest.cpp \
  RouteHandleTest.cpp \
  WeightedCh3HashFuncTest.cpp

mcrouter_lib_test_CPPFLAGS = \
  -I$(top_srcdir)/.. \
  -isystem $(top_srcdir)/lib/gtest/include
mcrouter_lib_test_LDADD = \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la
