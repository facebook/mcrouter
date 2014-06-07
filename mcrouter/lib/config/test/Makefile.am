noinst_PROGRAMS = mcrouter_config_test

mcrouter_config_test_SOURCES = \
  config_preprocessor_test.cpp \
  routehandlefactory_test.cpp

mcrouter_config_test_CPPFLAGS = -I$(top_srcdir)/oss_include
mcrouter_config_test_LDADD = $(top_builddir)/lib/libmcrouter.a -lgtest -lgtest_main
