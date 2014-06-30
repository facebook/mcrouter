noinst_PROGRAMS = mcrouter_fbi_test

mcrouter_fbi_test_SOURCES = \
  asox_queue_test.cpp \
  asox_sem_test.cpp \
  counter_test.cpp \
  cwlock_test.cpp \
  hash_test.cpp \
  log_tid_tests.cpp \
  sem_test.cpp \
  sfrlock_test.cpp \
  test_util.cpp

mcrouter_fbi_test_CPPFLAGS = -I$(top_srcdir)/oss_include
mcrouter_fbi_test_LDADD = $(top_builddir)/lib/libmcrouter.a -lgtest -lgtestmain
