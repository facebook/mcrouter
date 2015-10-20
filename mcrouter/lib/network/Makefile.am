SUBDIRS = . test

noinst_PROGRAMS = mock_mc_server
noinst_LIBRARIES = libtest_util.a

mock_mc_server_SOURCES = \
  test/MockMc.cpp \
  test/MockMc.h \
  test/MockMcServer.cpp

mock_mc_server_CPPFLAGS = -I$(top_srcdir)/..
mock_mc_server_LDADD = $(top_builddir)/lib/libmcrouter.a

libtest_util_a_SOURCES = \
  test/ClientSocket.cpp \
  test/ClientSocket.h \
  test/ListenSocket.cpp \
  test/ListenSocket.h

libtest_util_a_CPPFLAGS = -I$(top_srcdir)/..
