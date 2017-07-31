check_PROGRAMS = mcrouter_test

mcrouter_test_SOURCES = \
  awriter_test.cpp \
  config_api_test.cpp \
  exponential_smooth_data_test.cpp \
  file_observer_test.cpp \
  flavor_test.cpp \
  LeaseTokenMapTest.cpp \
  mc_route_handle_provider_test.cpp \
  McrouterClientUsage.cpp \
  observable_test.cpp \
  options_test.cpp \
  pool_factory_test.cpp \
  ProxyRequestContextTest.cpp \
  route_test.cpp \
  runtime_vars_data_test.cpp

mcrouter_test_CPPFLAGS = -I$(top_srcdir)/.. -isystem $(top_srcdir)/lib/gtest/include
mcrouter_test_LDADD = \
  $(top_builddir)/libmcroutercore.a \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la \
  $(top_builddir)/lib/network/libtest_util.a
