check_PROGRAMS = mcrouter_fbi_test

mcrouter_fbi_test_SOURCES = \
	main.cpp \
  sem_test.cpp \
  test_util.cpp \
	hash_test

mcrouter_fbi_test_CPPFLAGS = \
	-I$(top_srcdir)/.. \
	-isystem $(top_srcdir)/lib/gtest/include

mcrouter_fbi_test_LDADD = \
	$(top_builddir)/lib/libmcrouter.a \
	$(top_builddir)/lib/libtestmain.la
