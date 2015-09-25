check_PROGRAMS = mcrouter_cycles_test

mcrouter_cycles_test_SOURCES = \
  CyclesTest.cpp \
  IntervalTest.cpp \
  QuantilesCalculatorTest.cpp

mcrouter_cycles_test_CPPFLAGS = -I$(top_srcdir)/.. -I$(top_srcdir)/lib/gtest/include
mcrouter_cycles_test_LDADD = $(top_builddir)/lib/libmcrouter.a $(top_builddir)/lib/libgtestmain.la
