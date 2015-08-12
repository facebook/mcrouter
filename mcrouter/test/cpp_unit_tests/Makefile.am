check_PROGRAMS = mcrouter_test mcrouter_libmc_test

mcrouter_test_SOURCES = \
  awriter_test.cpp \
  config_api_test.cpp \
  file_observer_test.cpp \
  flavor_test.cpp \
  mc_route_handle_provider_test.cpp \
  mcrouter_cpp_tests.cpp \
  mcrouter_cpp_tests.h \
  observable_test.cpp \
  options_test.cpp \
  route_test.cpp \
  runtime_vars_data_test.cpp \
  TokenBucketTest.cpp

mcrouter_test_CPPFLAGS = -I$(top_srcdir)/..
mcrouter_test_LDADD = $(top_builddir)/libmcroutercore.a $(top_builddir)/lib/libmcrouter.a -lgtest -lgtestmain

mcrouter_libmc_test_SOURCES = \
  libmcrouter_test.cpp \
  mcrouter_test_client.cpp \
  mcrouter_test_client.h \
  MemcacheLocal.cpp \
  MemcacheLocal.h

mcrouter_libmc_test_CPPFLAGS = -I$(top_srcdir)/..
mcrouter_libmc_test_LDADD = $(top_builddir)/libmcroutercore.a $(top_builddir)/lib/libmcrouter.a -lgtest
