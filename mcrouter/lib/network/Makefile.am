SUBDIRS = . test

noinst_PROGRAMS = mock_mc_server

mock_mc_server_SOURCES = \
  test/MockMc.cpp \
  test/MockMc.h \
  test/MockMcServer.cpp

mock_mc_server_CPPFLAGS = -I$(top_srcdir)/..
mock_mc_server_LDADD = $(top_builddir)/lib/libmcrouter.a
