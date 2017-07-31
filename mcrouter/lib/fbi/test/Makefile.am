check_PROGRAMS = mcrouter_fbi_hash_test mcrouter_fbi_test

mcrouter_fbi_hash_test_SOURCES = hash_test.cpp

mcrouter_fbi_hash_test_CPPFLAGS = -I$(top_srcdir)/.. -isystem $(top_srcdir)/lib/gtest/include
mcrouter_fbi_hash_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libtestmain.la


mcrouter_fbi_test_SOURCES = \
  sem_test.cpp \
  test_util.cpp
#  Too slow
#  sfrlock_test.cpp

mcrouter_fbi_test_CPPFLAGS = -I$(top_srcdir)/.. -isystem $(top_srcdir)/lib/gtest/include
mcrouter_fbi_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libtestmain.la
