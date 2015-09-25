check_PROGRAMS = mcrouter_config_test

mcrouter_config_test_SOURCES = \
  config_preprocessor_test.cpp

mcrouter_config_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_config_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libgtestmain.la
