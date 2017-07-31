check_PROGRAMS = mcrouter_fbi_cpp_test

mcrouter_fbi_cpp_test_SOURCES = \
  TrieTests.cpp

mcrouter_fbi_cpp_test_CPPFLAGS = -I$(top_srcdir)/.. -isystem $(top_srcdir)/lib/gtest/include
mcrouter_fbi_cpp_test_LDADD = \
  $(top_builddir)/lib/libmcrouter.a \
  $(top_builddir)/lib/libtestmain.la
