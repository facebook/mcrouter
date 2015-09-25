check_PROGRAMS = mcrouter_fbi_hash_test mcrouter_fbi_test

mcrouter_fbi_hash_test_SOURCES = hash_test.cpp

mcrouter_fbi_hash_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_fbi_hash_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libgtestmain.la


mcrouter_fbi_test_SOURCES = \
  asox_queue_test.cpp \
  asox_sem_test.cpp \
  cwlock_test.cpp \
  log_tid_tests.cpp \
  sem_test.cpp \
  test_util.cpp
#  Too slow
#  sfrlock_test.cpp

mcrouter_fbi_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_fbi_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libgtestmain.la
